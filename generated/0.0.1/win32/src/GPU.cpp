#include "GPU.h"
#include "GPUAdapter.h"

Napi::FunctionReference GPU::constructor;

GPU::GPU(const Napi::CallbackInfo& info) : Napi::ObjectWrap<GPU>(info) { }
GPU::~GPU() { }

Napi::Value GPU::requestAdapter(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  auto deferred = Napi::Promise::Deferred::New(env);

  std::vector<napi_value> args = {};
  if (info[0].IsObject()) args.push_back(info[0].As<Napi::Value>());

  deferred.Resolve(GPUAdapter::constructor.New(args));

  return deferred.Promise();
}

Napi::Object GPU::Initialize(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);
  Napi::Function func = DefineClass(env, "GPU", {
    StaticMethod(
      "requestAdapter",
      &GPU::requestAdapter,
      napi_enumerable
    )
  });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("GPU", func);
  return exports;
}
