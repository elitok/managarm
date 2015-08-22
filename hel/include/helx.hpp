
namespace helx {

inline void panic(const char *string) {
	int length = 0;
	for(int i = 0; string[i] != 0; i++)
		length++;
	helPanic(string, length);
}

typedef void (*RecvStringFunction) (void *, HelError, int64_t, int64_t, size_t);
typedef void (*RecvDescriptorFunction) (void *, HelError, int64_t, int64_t, HelHandle);
typedef void (*AcceptFunction) (void *, HelError, HelHandle);
typedef void (*ConnectFunction) (void *, HelError, HelHandle);
typedef void (*IrqFunction) (void *, HelError);

class EventHub {
public:
	enum { kEventsPerCall = 16 };

	inline EventHub() {
		HEL_CHECK(helCreateEventHub(&p_handle));
	}

	inline HelHandle getHandle() {
		return p_handle;
	}

	inline void defaultProcessEvents() {
		HelEvent list[kEventsPerCall];
		size_t num_items;
		HEL_CHECK(helWaitForEvents(p_handle, list, kEventsPerCall,
				kHelWaitInfinite, &num_items));

		for(int i = 0; i < num_items; i++) {
			HelEvent &evt = list[i];
			switch(evt.type) {
			case kHelEventRecvString: {
				auto function = (RecvStringFunction)evt.submitFunction;
				function((void *)evt.submitObject, evt.error,
						evt.msgRequest, evt.msgSequence, evt.length);
			} break;
			case kHelEventRecvDescriptor: {
				auto function = (RecvDescriptorFunction)evt.submitFunction;
				function((void *)evt.submitObject, evt.error,
						evt.msgRequest, evt.msgSequence, evt.handle);
			} break;
			case kHelEventAccept: {
				auto function = (AcceptFunction)evt.submitFunction;
				function((void *)evt.submitObject, evt.error, evt.handle);
			} break;
			case kHelEventConnect: {
				auto function = (ConnectFunction)evt.submitFunction;
				function((void *)evt.submitObject, evt.error, evt.handle);
			} break;
			case kHelEventIrq: {
				auto function = (IrqFunction)evt.submitFunction;
				function((void *)evt.submitObject, evt.error);
			} break;
			default:
				panic("Unknown event type");
			}
		}
	}

	inline HelEvent waitForEvent(int64_t async_id) {
		while(true) {
			HelEvent event;
			size_t num_items;
			HEL_CHECK(helWaitForEvents(p_handle, &event, 1,
					kHelWaitInfinite, &num_items));

			if(num_items == 0)
				continue;
			//ASSERT(event.asyncId == async_id);
			return event;
		}
	}

	inline size_t waitForRecvString(int64_t async_id) {
		HelEvent event = waitForEvent(async_id);
		return event.length;
	}
	inline HelHandle waitForRecvDescriptor(int64_t async_id) {
		HelEvent event = waitForEvent(async_id);
		return event.handle;
	}
	inline HelHandle waitForConnect(int64_t async_id) {
		HelEvent event = waitForEvent(async_id);
		return event.handle;
	}

private:
	HelHandle p_handle;
};

class Pipe {
public:
	static void createBiDirection(Pipe &first, Pipe &second) {
		HelHandle first_handle, second_handle;
		HEL_CHECK(helCreateBiDirectionPipe(&first_handle, &second_handle));
		first = Pipe(first_handle);
		second = Pipe(second_handle);
	}

	inline Pipe() : p_handle(kHelNullHandle) { }

	inline Pipe(HelHandle handle) : p_handle(handle) { }
	
	inline HelHandle getHandle() {
		return p_handle;
	}

	inline void sendString(const void *buffer, size_t length,
			int64_t msg_request, int64_t msg_seq) {
		HEL_CHECK(helSendString(p_handle, (const uint8_t *)buffer, length,
				msg_request, msg_seq));
	}

	inline void sendDescriptor(HelHandle send_handle,
			int64_t msg_request, int64_t msg_seq) {
		HEL_CHECK(helSendDescriptor(p_handle, send_handle, msg_request, msg_seq));
	}

	inline void recvString(void *buffer, size_t length,
			EventHub &event_hub, int64_t msg_request, int64_t msg_seq,
			void *object, RecvStringFunction function) {
		int64_t async_id;
		HEL_CHECK(helSubmitRecvString(p_handle, event_hub.getHandle(),
				(uint8_t *)buffer, length, msg_request, msg_seq,
				(uintptr_t)function, (uintptr_t)object, &async_id));
	}
	
	inline void recvDescriptor(EventHub &event_hub,
			int64_t msg_request, int64_t msg_seq,
			void *object, RecvDescriptorFunction function) {
		int64_t async_id;
		HEL_CHECK(helSubmitRecvDescriptor(p_handle, event_hub.getHandle(),
				msg_request, msg_seq, (uintptr_t)function, (uintptr_t)object, &async_id));
	}

private:
	HelHandle p_handle;
};

class Server {
public:
	inline Server(HelHandle handle) : p_handle(handle) { }

	inline HelHandle getHandle() {
		return p_handle;
	}

	inline void accept(EventHub &event_hub,
			void *object, AcceptFunction function) {
		int64_t async_id;
		HEL_CHECK(helSubmitAccept(p_handle, event_hub.getHandle(),
				(uintptr_t)function, (uintptr_t)object, &async_id));
	}
	
	inline void connect(EventHub &event_hub,
			void *object, ConnectFunction function) {
		int64_t async_id;
		HEL_CHECK(helSubmitConnect(p_handle, event_hub.getHandle(),
				(uintptr_t)function, (uintptr_t)object, &async_id));
	}

private:
	HelHandle p_handle;
};

} // namespace helx

