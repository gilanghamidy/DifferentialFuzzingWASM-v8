
#include "v8.h"

#include <vector>
#include <map>
#include <string>

namespace v8 {
namespace ext {

void V8_EXPORT HelloDarling();

class CompiledWasm;

Maybe<CompiledWasm> V8_EXPORT CompileBinaryWasm(Isolate* i, const uint8_t* arr, size_t len);

class CompiledWasmFunction {
  struct Internal;
  Internal* internal;
  friend Maybe<CompiledWasm> v8::ext::CompileBinaryWasm(Isolate* i, const uint8_t* arr, size_t len);
public:
  V8_EXPORT CompiledWasmFunction(CompiledWasmFunction&& that);
  CompiledWasmFunction& V8_EXPORT operator=(CompiledWasmFunction&& that);
  V8_EXPORT CompiledWasmFunction(CompiledWasmFunction const& that);
  CompiledWasmFunction& V8_EXPORT operator=(CompiledWasmFunction const& that);
  CompiledWasmFunction();

  V8_EXPORT ~CompiledWasmFunction();
  MaybeLocal<Value> V8_EXPORT Invoke(Isolate* i, std::vector<Local<Value>>& args) const;
};

class CompiledWasm {
  std::vector<CompiledWasmFunction> functions;
  std::map<std::string, size_t> function_names;

  friend Maybe<CompiledWasm> v8::ext::CompileBinaryWasm(Isolate* i, const uint8_t* arr, size_t len);
public:
  CompiledWasmFunction const& FunctionByIndex(size_t idx) { return functions[idx]; }
  CompiledWasmFunction const& FunctionByName(std::string const& name) { return functions[function_names[name]]; }
};



} // namespace ext
} // namespace v8