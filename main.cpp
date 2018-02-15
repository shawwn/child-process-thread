#include <v8.h>
#include <nan.h>
#include <node.h>
#include <uv.h>

#include <pthread.h>
#include <memory>

using namespace v8;
using namespace node;
using namespace std;

#define JS_STR(...) Nan::New<v8::String>(__VA_ARGS__).ToLocalChecked()
#define JS_INT(val) Nan::New<v8::Integer>(val)
#define JS_NUM(val) Nan::New<v8::Number>(val)
#define JS_FLOAT(val) Nan::New<v8::Number>(val)
#define JS_BOOL(val) Nan::New<v8::Boolean>(val)

namespace childProcessThread {

uv_key_t threadKey;

class QueueEntry {
public:
  QueueEntry(uintptr_t address, size_t size) : address(address), size(size) {}
  QueueEntry(const QueueEntry &queueEntry) : address(queueEntry.address), size(queueEntry.size) {}

  uintptr_t address;
  size_t size;
};

class Thread : public Nan::ObjectWrap {
public:
  static Handle<Object> Initialize();
  const string &getJsPath() const;
  const vector<pair<string, uintptr_t>> &getImports() const;
  pthread_t &getThread();
  uv_loop_t &getLoop();
  uv_async_t &getExitAsync();
  uv_mutex_t &getMutex();
  void pushMessageIn(const QueueEntry &queueEntry);
  void pushMessageOut(const QueueEntry &queueEntry);
  queue<QueueEntry> getMessageQueueIn();
  queue<QueueEntry> getMessageQueueOut();
  static const string &getChildJsPath();
  static void setChildJsPath(const string &childJsPath);
  static Thread *getCurrentThread();
  static void setCurrentThread(Thread *thread);
  void setGlobal(Local<Object> global);
  Local<Object> getGlobal();
  bool getLive() const;
  void setLive(bool live);

  Thread(const string &jsPath, const vector<pair<string, uintptr_t>> &imports);
  ~Thread();
  static NAN_METHOD(New);
  static NAN_METHOD(Fork);
  static NAN_METHOD(SetChildJsPath);
  static NAN_METHOD(Terminate);
  static NAN_METHOD(Cancel);
  static NAN_METHOD(PostThreadMessageIn);
  static NAN_METHOD(PostThreadMessageOut);
  static NAN_METHOD(PollThreadMessagesOut);

private:
  static string childJsPath;

  string jsPath;
  vector<pair<string, uintptr_t>> imports;
  uv_loop_t loop;
  uv_async_t exitAsync;
  uv_mutex_t mutex;
  uv_async_t messageAsyncIn;
  pthread_t thread;
  Persistent<Object> global;
  queue<QueueEntry> messageQueueIn;
  queue<QueueEntry> messageQueueOut;
  bool live;
};

/* static Mutex node_isolate_mutex;
static v8::Isolate *node_isolate; */

/* bool ShouldAbortOnUncaughtException(Isolate* isolate) {
  HandleScope scope(isolate);
  Environment* env = Environment::GetCurrent(isolate);
  return env->should_abort_on_uncaught_toggle()[0] &&
         !env->inside_should_not_abort_on_uncaught_scope();
} */

inline int Start(
  Thread *thread, Isolate* isolate, IsolateData* isolate_data,
  int argc, const char* const* argv,
  int exec_argc, const char* const* exec_argv
) {
  HandleScope handle_scope(isolate);
  Local<Context> context = Context::New(isolate);
  // Local<Context> context = NewContext(isolate);
  Context::Scope context_scope(context);

  {
    Local<Object> global = context->Global();

    Local<Object> importsObj = Nan::New<Object>();
    const vector<pair<string, uintptr_t>> imports = thread->getImports();
    for (size_t i = 0; i < imports.size(); i++) {
      const pair<string, uintptr_t> &import = imports[i];
      const string &name = import.first;
      const uintptr_t address = import.second;

      void (*Init)(Handle<Object> exports) = (void (*)(Handle<Object>))address;
      Local<Object> exportsObj = Nan::New<Object>();
      Init(exportsObj);

      // XXX

      importsObj->Set(Nan::New<String>(name).ToLocalChecked(), exportsObj);
    }
    global->Set(JS_STR("imports"), importsObj);
    global->Set(JS_STR("onthreadmessage"), Nan::Null());
    global->Set(JS_STR("postThreadMessage"), Nan::New<Function>(Thread::PostThreadMessageOut));

    thread->setGlobal(global);
  }

  Environment *env = CreateEnvironment(isolate_data, context, argc, argv, exec_argc, exec_argv);

  uv_key_t thread_local_env;
  uv_key_create(&thread_local_env);
  // CHECK_EQ(0, uv_key_create(&thread_local_env));
  uv_key_set(&thread_local_env, env);
  // env->Start(argc, argv, exec_argc, exec_argv, v8_is_profiling);

  LoadEnvironment(env);

  /* const char* path = argc > 1 ? argv[1] : nullptr;
  StartInspector(&env, path, debug_options);

  if (debug_options.inspector_enabled() && !v8_platform.InspectorStarted(&env))
    return 12;  // Signal internal error.

  env->set_abort_on_uncaught_exception(abort_on_uncaught_exception);

  if (no_force_async_hooks_checks) {
    env->async_hooks()->no_force_checks();
  }

  {
    Environment::AsyncCallbackScope callback_scope(env);
    env->async_hooks()->push_async_ids(1, 0);
    LoadEnvironment(env);
    env->async_hooks()->pop_async_id(1);
  }

  env->set_trace_sync_io(trace_sync_io); */


  {
    SealHandleScope seal(isolate);
    bool more;
    // PERFORMANCE_MARK(&env, LOOP_START)

    thread->setLive(true);

    do {
      uv_run(&thread->getLoop(), UV_RUN_ONCE);

      // v8_platform.DrainVMTasks(isolate);

      more = uv_loop_alive(&thread->getLoop()) && thread->getLive();
      /* if (more) {
        continue;
      } */

      // EmitBeforeExit(env);

      // Emit `beforeExit` if the loop became alive either after emitting
      // event, or after running some callbacks.
      // more = uv_loop_alive(&thread->getLoop());
    } while (more == true);
    // PERFORMANCE_MARK(&env, LOOP_EXIT);
  }

  // env.set_trace_sync_io(false);

  /* const int exit_code = EmitExit(&env);
  RunAtExit(&env); */
  uv_key_delete(&thread_local_env);

  FreeEnvironment(env);

  /* v8_platform.DrainVMTasks(isolate);
  v8_platform.CancelVMTasks(isolate);
  WaitForInspectorDisconnect(&env);
#if defined(LEAK_SANITIZER)
  __lsan_do_leak_check();
#endif */

  return 0;
}

inline int Start(Thread *thread,
  int argc, const char* const* argv,
  int exec_argc, const char* const* exec_argv
) {
  Isolate::CreateParams params;
  unique_ptr<ArrayBuffer::Allocator> allocator(ArrayBuffer::Allocator::NewDefaultAllocator());
  params.array_buffer_allocator = allocator.get();
/* #ifdef NODE_ENABLE_VTUNE_PROFILING
  params.code_event_handler = vTune::GetVtuneCodeEventHandler();
#endif */

  Isolate* const isolate = Isolate::New(params);
  if (isolate == nullptr)
    return 12;  // Signal internal error.

  // isolate->AddMessageListener(OnMessage);
  // isolate->SetAbortOnUncaughtExceptionCallback(ShouldAbortOnUncaughtException);
  isolate->SetAutorunMicrotasks(false);
  // isolate->SetFatalErrorHandler(OnFatalError);

  /* {
    Mutex::ScopedLock scoped_lock(node_isolate_mutex);
    CHECK_EQ(node_isolate, nullptr);
    node_isolate = isolate;
  } */

  int exit_code;
  {
    Locker locker(isolate);
    Isolate::Scope isolate_scope(isolate);
    HandleScope handle_scope(isolate);
    IsolateData *isolate_data = CreateIsolateData(isolate, &thread->getLoop());
    /* IsolateData isolate_data(
        isolate,
        event_loop,
        // v8_platform.Platform(),
        nullptr,
        allocator.zero_fill_field());
    if (track_heap_objects) {
      isolate->GetHeapProfiler()->StartTrackingHeapObjects(true);
    } */
    exit_code = Start(thread, isolate, isolate_data, argc, argv, exec_argc, exec_argv);

    FreeIsolateData(isolate_data);
  }

  /* {
    Mutex::ScopedLock scoped_lock(node_isolate_mutex);
    CHECK_EQ(node_isolate, isolate);
    node_isolate = nullptr;
  } */

  isolate->Dispose();

  return exit_code;
}

static void *threadFn(void *arg) {
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);

  Thread *thread = (Thread *)arg;
  Thread::setCurrentThread(thread);

  char argsString[4096];
  int i = 0;

  char *binPathArg = argsString + i;
  const char *binPathString = "node";
  strncpy(binPathArg, binPathString, sizeof(argsString) - i);
  i += strlen(binPathString) + 1;

  char *childJsPathArg = argsString + i;
  strncpy(childJsPathArg, Thread::getChildJsPath().c_str(), sizeof(argsString) - i);
  i += Thread::getChildJsPath().length() + 1;

  char *jsPathArg = argsString + i;
  strncpy(jsPathArg, thread->getJsPath().c_str(), sizeof(argsString) - i);
  i += thread->getJsPath().length() + 1;

  char *argv[] = {binPathArg, childJsPathArg, jsPathArg};
  int argc = sizeof(argv)/sizeof(argv[0]);
  int retval = Start(thread, argc, argv, argc, argv);

  return new int(retval);
}
void exitAsyncCb(uv_async_t *handle) {
  Thread *thread = Thread::getCurrentThread();
  thread->setLive(false);
}
void messageAsyncInCb(uv_async_t *handle) {
  Nan::HandleScope handleScope;

  Thread *thread = Thread::getCurrentThread();

  queue<QueueEntry> messageQueue(thread->getMessageQueueIn());

  Local<Object> global = thread->getGlobal();
  Local<Value> onthreadmessageValue = global->Get(JS_STR("onthreadmessage"));
  if (onthreadmessageValue->IsFunction()) {
    Local<Function> onthreadmessageFn = Local<Function>::Cast(onthreadmessageValue);

    for (size_t i = 0; i < messageQueue.size(); i++) {
      const QueueEntry &queueEntry = messageQueue.back();
      messageQueue.pop();

      char *data = (char *)queueEntry.address;
      size_t size = queueEntry.size;
      Local<ArrayBuffer> message = ArrayBuffer::New(Isolate::GetCurrent(), data, size);

      Local<Value> argv[] = {message};
      onthreadmessageFn->Call(Nan::Null(), sizeof(argv)/sizeof(argv[0]), argv);
    }
  }
}

Handle<Object> Thread::Initialize() {
  Nan::EscapableHandleScope scope;

  // constructor
  Local<FunctionTemplate> ctor = Nan::New<FunctionTemplate>(New);
  ctor->InstanceTemplate()->SetInternalFieldCount(1);
  ctor->SetClassName(JS_STR("Thread"));
  Nan::SetPrototypeMethod(ctor, "terminate", Thread::Terminate);
  Nan::SetPrototypeMethod(ctor, "cancel", Thread::Cancel);
  Nan::SetPrototypeMethod(ctor, "postThreadMessage", Thread::PostThreadMessageIn);
  Nan::SetPrototypeMethod(ctor, "pollThreadMessages", Thread::PollThreadMessagesOut);

  Local<Function> ctorFn = ctor->GetFunction();

  Local<Function> forkFn = Nan::New<Function>(Thread::Fork);
  forkFn->Set(JS_STR("Thread"), ctorFn);
  ctorFn->Set(JS_STR("fork"), forkFn);
  ctorFn->Set(JS_STR("setChildJsPath"), Nan::New<Function>(Thread::SetChildJsPath));

  return scope.Escape(ctorFn);
}
const string &Thread::getJsPath() const {
  return jsPath;
}
const vector<pair<string, uintptr_t>> &Thread::getImports() const {
  return imports;
}
pthread_t &Thread::getThread() {
  return thread;
}
uv_loop_t &Thread::getLoop() {
  return loop;
}
uv_async_t &Thread::getExitAsync() {
  return exitAsync;
}
uv_mutex_t &Thread::getMutex() {
  return mutex;
}
void Thread::pushMessageIn(const QueueEntry &queueEntry) {
  uv_mutex_lock(&mutex);

  messageQueueIn.push(queueEntry);

  uv_async_send(&messageAsyncIn);

  uv_mutex_unlock(&mutex);
}
void Thread::pushMessageOut(const QueueEntry &queueEntry) {
  uv_mutex_lock(&mutex);

  messageQueueOut.push(queueEntry);

  uv_async_send(&messageAsyncIn);

  uv_mutex_unlock(&mutex);
}
queue<QueueEntry> Thread::getMessageQueueIn() {
  uv_mutex_lock(&mutex);

  queue<QueueEntry> result;
  messageQueueIn.swap(result);

  uv_mutex_unlock(&mutex);

  return result;
}
queue<QueueEntry> Thread::getMessageQueueOut() {
  uv_mutex_lock(&mutex);

  queue<QueueEntry> result;
  messageQueueOut.swap(result);

  uv_mutex_unlock(&mutex);

  return result;
}
const string &Thread::getChildJsPath() {
  return Thread::childJsPath;
}
void Thread::setChildJsPath(const string &childJsPath) {
  Thread::childJsPath = childJsPath;
}
Thread *Thread::getCurrentThread() {
  return (Thread *)uv_key_get(&threadKey);
}
void Thread::setCurrentThread(Thread *thread) {
  uv_key_set(&threadKey, thread);
}
void Thread::setGlobal(Local<Object> global) {
  this->global.Reset(Isolate::GetCurrent(), global);
}
Local<Object> Thread::getGlobal() {
  return Nan::New(global);
}
bool Thread::getLive() const {
  return live;
}
void Thread::setLive(bool live) {
  this->live = live;
}
Thread::Thread(const string &jsPath, const vector<pair<string, uintptr_t>> &imports) : jsPath(jsPath), imports(imports) {
  uv_loop_init(&loop);
  uv_async_init(&loop, &exitAsync, exitAsyncCb);
  uv_mutex_init(&mutex);
  uv_async_init(&loop, &messageAsyncIn, messageAsyncInCb);

  live = true;

  pthread_create(&thread, nullptr, threadFn, this);
}
Thread::~Thread() {
  uv_close((uv_handle_t *)&exitAsync, nullptr);
  uv_close((uv_handle_t *)&messageAsyncIn, nullptr);
  uv_close((uv_handle_t *)&loop, nullptr);
}
NAN_METHOD(Thread::New) {
  Nan::HandleScope scope;

  if (info[0]->IsString() && info[1]->IsObject()) {
    Local<Object> rawThreadObj = info.This();

    Local<String> jsPathValue = info[0]->ToString();
    String::Utf8Value jsPathValueUtf8(jsPathValue);
    size_t length = jsPathValueUtf8.length();
    string jsPath(*jsPathValueUtf8, length);

    Local<Object> importsObject = Local<Object>::Cast(info[1]);
    Local<Array> importsObjectKeys = importsObject->GetOwnPropertyNames();
    vector<pair<string, uintptr_t>> imports;
    for (size_t i = 0; i < importsObjectKeys->Length(); i++) {
      Local<String> key = Local<String>::Cast(importsObjectKeys->Get(i));
      Local<Number> value = Local<Number>::Cast(importsObject->Get(key));

      String::Utf8Value keyValueUtf8(key);
      string importName(*keyValueUtf8, keyValueUtf8.length());

      double numberValue = value->NumberValue();
      uintptr_t address = *reinterpret_cast<uintptr_t*>(&numberValue);

      imports.emplace_back(importName, address);
    }

    Thread *thread = new Thread(jsPath, imports);
    thread->Wrap(rawThreadObj);

    info.GetReturnValue().Set(rawThreadObj);
  } else {
    return Nan::ThrowError("Invalid arguments");
  }
}
NAN_METHOD(Thread::Fork) {
  if (info[0]->IsString() && info[1]->IsObject()) {
    Local<Function> threadConstructor = Local<Function>::Cast(info.Callee()->Get(JS_STR("Thread")));
    Local<Value> argv[] = {
      info[0],
      info[1],
    };
    Local<Value> threadObj = threadConstructor->NewInstance(sizeof(argv)/sizeof(argv[0]), argv);

    info.GetReturnValue().Set(threadObj);
  } else {
    Nan::ThrowError("Invalid arguments");
  }
}
NAN_METHOD(Thread::SetChildJsPath) {
  if (info[0]->IsString()) {
    Local<String> childJsPathValue = Local<String>::Cast(info[0]);
    String::Utf8Value childJsPathValueUtf8(info[0]);
    size_t length = childJsPathValueUtf8.length();
    string childJsPath(*childJsPathValueUtf8, length);

    Thread::setChildJsPath(childJsPath);
  } else {
    Nan::ThrowError("Invalid arguments");
  }
}
NAN_METHOD(Thread::Terminate) {
  Thread *thread = ObjectWrap::Unwrap<Thread>(info.This());

  uv_async_send(&thread->getExitAsync());

  void *retval;
  int result = pthread_join(thread->getThread(), &retval);

  info.GetReturnValue().Set(Nan::New<Integer>(result == 0 ? (*(int *)retval) : 1));
}
NAN_METHOD(Thread::Cancel) {
  Thread *thread = ObjectWrap::Unwrap<Thread>(info.This());

  pthread_cancel(thread->getThread());

  pthread_join(thread->getThread(), nullptr);
}
NAN_METHOD(Thread::PostThreadMessageIn) {
  if (info[0]->IsArrayBuffer()) {
    Thread *thread = ObjectWrap::Unwrap<Thread>(info.This());

    Local<ArrayBuffer> arrayBuffer = Local<ArrayBuffer>::Cast(info[0]);

    if (arrayBuffer->IsExternal()) {
      QueueEntry queueEntry((uintptr_t)arrayBuffer->GetContents().Data(), arrayBuffer->ByteLength());

      thread->pushMessageIn(queueEntry);
    } else {
      return Nan::ThrowError("ArrayBuffer is not external");
    }
  } else {
    return Nan::ThrowError("invalid arguments");
  }
}
NAN_METHOD(Thread::PostThreadMessageOut) {
  if (info[0]->IsArrayBuffer()) {
    Thread *thread = Thread::getCurrentThread();

    Local<ArrayBuffer> arrayBuffer = Local<ArrayBuffer>::Cast(info[0]);

    if (arrayBuffer->IsExternal()) {
      QueueEntry queueEntry((uintptr_t)arrayBuffer->GetContents().Data(), arrayBuffer->ByteLength());

      thread->pushMessageOut(queueEntry);
    } else {
      return Nan::ThrowError("ArrayBuffer is not external");
    }
  } else {
    return Nan::ThrowError("invalid arguments");
  }
}
NAN_METHOD(Thread::PollThreadMessagesOut) {
  Thread *thread = ObjectWrap::Unwrap<Thread>(info.This());

  queue<QueueEntry> messageQueue(thread->getMessageQueueOut());
  if (messageQueue.size() > 0) {
    Local<Array> result = Nan::New<Array>(messageQueue.size());
    for (size_t i = 0; i < messageQueue.size(); i++) {
      const QueueEntry &queueEntry = messageQueue.back();
      messageQueue.pop();

      char *data = (char *)queueEntry.address;
      size_t size = queueEntry.size;
      Local<ArrayBuffer> message = ArrayBuffer::New(Isolate::GetCurrent(), data, size);
      result->Set(i, message);
    }
    info.GetReturnValue().Set(result);
  } else {
    info.GetReturnValue().Set(Nan::Null());
  }
}
void Init(Handle<Object> exports) {
  uv_key_create(&threadKey);

  exports->Set(JS_STR("Thread"), Thread::Initialize());
}
string Thread::childJsPath;

NODE_MODULE(NODE_GYP_MODULE_NAME, Init)

}
