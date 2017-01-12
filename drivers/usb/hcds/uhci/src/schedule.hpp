
#include <queue>
#include <async/doorbell.hpp>

struct DeviceState;
struct ConfigurationState;
struct InterfaceState;
struct EndpointState;

// ----------------------------------------------------------------------------
// Memory management.
// ----------------------------------------------------------------------------

template<typename T>
struct contiguous_delete {
	void operator() (T *pointer) {
		contiguousAllocator.free(pointer);
	}
};

template<typename T>
struct contiguous_delete<T[]> {
	void operator() (T *pointer);
};

template<typename T>
using contiguous_ptr = std::unique_ptr<T, contiguous_delete<T>>;

template<typename T, typename... Args>
contiguous_ptr<T> make_contiguous(Args &&... args);

// ----------------------------------------------------------------
// Controller.
// ----------------------------------------------------------------

struct Controller : std::enable_shared_from_this<Controller> {
	friend struct ConfigurationState;

	Controller(uint16_t base, helix::UniqueIrq irq);

	void initialize();
	async::result<void> pollDevices();
	async::result<void> probeDevice();
	cofiber::no_future handleIrqs();

private:
	uint16_t _base;
	helix::UniqueIrq _irq;

	uint16_t _lastFrame;
	int64_t _frameCounter;
	async::doorbell _pollDoorbell;

	void _updateFrame();

	// ------------------------------------------------------------------------
	// Schedule classes.
	// ------------------------------------------------------------------------

	// Base class for classes that represent elements of the UHCI schedule.
	// All those classes are linked into a list that represents a part of the schedule.
	// They need to be freed through the reclaim mechansim.
	struct ScheduleItem : boost::intrusive::list_base_hook<> {
		ScheduleItem()
		: reclaimFrame(-1) { }

		virtual ~ScheduleItem() {
			assert(reclaimFrame != -1);
		}

		int64_t reclaimFrame;
	};

	struct Transaction : ScheduleItem {
		explicit Transaction(TransferDescriptor *transfers, size_t num_transfers)
		: transfers{transfers}, numTransfers{num_transfers}, numComplete{0} { }
		
		TransferDescriptor *transfers;
		size_t numTransfers;
		size_t numComplete;
		async::promise<void> promise;
	};

	struct QueueEntity : ScheduleItem {
		QueueEntity()
		: head{make_contiguous<QueueHead>()} {
			head->_linkPointer = QueueHead::LinkPointer();
			head->_elementPointer = QueueHead::ElementPointer();
		}

		contiguous_ptr<QueueHead> head;
		boost::intrusive::list<Transaction> transactions;
	};

	// ------------------------------------------------------------------------
	// Device management.
	// ------------------------------------------------------------------------

	struct EndpointSlot {
		size_t maxPacketSize;
		QueueEntity *queueEntity;
	};

	struct DeviceSlot {
		EndpointSlot controlStates[16];
		EndpointSlot outStates[16];
		EndpointSlot inStates[16];
	};

	std::queue<int> _addressStack;
	DeviceSlot _activeDevices[128];

public:
	async::result<std::string> configurationDescriptor(int address);
	async::result<void> useConfiguration(int address, int configuration);
	async::result<void> useInterface(int address, int interface, int alternative);

	// ------------------------------------------------------------------------
	// Transfer functions.
	// ------------------------------------------------------------------------
	
	static Transaction *_buildControl(int address, int pipe, XferFlags dir,
			SetupPacket *setup, void *buffer, size_t length, size_t max_packet_size);
	static Transaction *_buildInterruptOrBulk(int address, int pipe, XferFlags dir,
			void *buffer, size_t length, size_t max_packet_size);

public:
	async::result<void> transfer(int address, int pipe, ControlTransfer info);
	async::result<void> transfer(int address, PipeType type, int pipe, InterruptTransfer info);
	async::result<void> transfer(int address, PipeType type, int pipe, BulkTransfer info);

private:
	async::result<void> _directTransfer(int address, int pipe, ControlTransfer info,
			QueueEntity *queue, size_t max_packet_size);

private:
	// ------------------------------------------------------------------------
	// Schedule management.
	// ------------------------------------------------------------------------
	
	void _linkInterrupt(QueueEntity *entity);
	void _linkAsync(QueueEntity *entity);
	void _linkTransaction(QueueEntity *queue, Transaction *transaction);

	void _progressSchedule();
	void _progressQueue(QueueEntity *entity);

	void _reclaim(ScheduleItem *item);

	boost::intrusive::list<QueueEntity> _interruptSchedule[1024];

	boost::intrusive::list<QueueEntity> _asyncSchedule;

	// This queue holds all schedule structs that are currently
	// being garbage collected.
	boost::intrusive::list<ScheduleItem> _reclaimQueue;

	QueueHead _periodicQh[1024];
	QueueHead _asyncQh;
	
	// ----------------------------------------------------------------------------
	// Debugging functions.
	// ----------------------------------------------------------------------------

	static void _dump(Transaction *transaction);
};

// ----------------------------------------------------------------------------
// DeviceState
// ----------------------------------------------------------------------------

struct DeviceState : DeviceData {
	explicit DeviceState(std::shared_ptr<Controller> controller, int device);

	async::result<std::string> configurationDescriptor() override;
	async::result<Configuration> useConfiguration(int number) override;
	async::result<void> transfer(ControlTransfer info) override;

private:
	std::shared_ptr<Controller> _controller;
	int _device;
};

// ----------------------------------------------------------------------------
// ConfigurationState
// ----------------------------------------------------------------------------

struct ConfigurationState : ConfigurationData {
	explicit ConfigurationState(std::shared_ptr<Controller> controller,
			int device, int configuration);

	async::result<Interface> useInterface(int number, int alternative) override;

private:
	std::shared_ptr<Controller> _controller;
	int _device;
	int _configuration;
};

// ----------------------------------------------------------------------------
// InterfaceState
// ----------------------------------------------------------------------------

struct InterfaceState : InterfaceData {
	explicit InterfaceState(std::shared_ptr<Controller> controller,
			int device, int configuration);

	async::result<Endpoint> getEndpoint(PipeType type, int number) override;

private:
	std::shared_ptr<Controller> _controller;
	int _device;
	int _interface;
};

// ----------------------------------------------------------------------------
// EndpointState
// ----------------------------------------------------------------------------

struct EndpointState : EndpointData {
	explicit EndpointState(std::shared_ptr<Controller> controller,
			int device, PipeType type, int endpoint);

	async::result<void> transfer(ControlTransfer info) override;
	async::result<void> transfer(InterruptTransfer info) override;
	async::result<void> transfer(BulkTransfer info) override;

private:
	std::shared_ptr<Controller> _controller;
	int _device;
	PipeType _type;
	int _endpoint;
};
