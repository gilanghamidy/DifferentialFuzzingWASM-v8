
#include "v8.h"

#include <vector>
#include <map>
#include <string>

namespace v8 {
namespace ext {

void V8_EXPORT HelloDarling();

class CompiledWasm;

Maybe<CompiledWasm> V8_EXPORT CompileBinaryWasm(Isolate* i, const uint8_t* arr, size_t len);

class V8_EXPORT CompiledWasmFunction {
  struct Internal;
  Internal* internal;
  uint32_t func_index;
  friend Maybe<CompiledWasm> v8::ext::CompileBinaryWasm(Isolate* i, const uint8_t* arr, size_t len);
public:
  CompiledWasmFunction();
  V8_EXPORT CompiledWasmFunction(CompiledWasmFunction&& that);
  CompiledWasmFunction& V8_EXPORT operator=(CompiledWasmFunction&& that);

  CompiledWasmFunction(CompiledWasmFunction const& that);
  CompiledWasmFunction& operator=(CompiledWasmFunction const& that);
  

  V8_EXPORT ~CompiledWasmFunction();
  MaybeLocal<Value> V8_EXPORT Invoke(Isolate* i, std::vector<Local<Value>>& args) const;
  std::vector<uint8_t> V8_EXPORT Instructions() const;
};

class CompiledWasm {
  std::vector<CompiledWasmFunction> functions;
  std::map<std::string, size_t> function_names;

  friend class CompiledWasmFunction;
  friend Maybe<CompiledWasm> v8::ext::CompileBinaryWasm(Isolate* i, const uint8_t* arr, size_t len);
public:


  CompiledWasmFunction const& FunctionByIndex(size_t idx) { return functions[idx]; }
  CompiledWasmFunction const& FunctionByName(std::string const& name) { 
    return functions[function_names.at(name)]; 
  }

  CompiledWasmFunction const& operator[](size_t idx) { return functions[idx]; }
  CompiledWasmFunction const& operator[](std::string const& name) { return FunctionByName(name); }

};

bool V8_EXPORT GenerateRandomWasm(Isolate* i, std::vector<uint8_t> const& input, std::vector<uint8_t>& output);

} // namespace ext
} // namespace v8