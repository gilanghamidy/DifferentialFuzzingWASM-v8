
#include "v8.h"

#include <vector>
#include <map>
#include <string>
#include <memory>
#include <functional>
namespace v8 {
namespace ext {

void V8_EXPORT HelloDarling();

class CompiledWasm;
class WasmInstance;

Maybe<CompiledWasm> V8_EXPORT CompileBinaryWasm(Isolate* i, const uint8_t* arr, size_t len);

enum class WasmType {
  Void,
  I32,
  I64,
  F32,
  F64
};

class V8_EXPORT CompiledWasmFunction {
  struct Internal;
  std::unique_ptr<Internal> internal;
  uint32_t func_index;
  std::reference_wrapper<CompiledWasm> parent;
  std::string name;

  friend Maybe<CompiledWasm> v8::ext::CompileBinaryWasm(Isolate* i, const uint8_t* arr, size_t len);
  friend class CompiledWasm;

  void Reattach(CompiledWasm& parent);

public:
  CompiledWasmFunction(CompiledWasm& parent);
  V8_EXPORT CompiledWasmFunction(CompiledWasmFunction&& that);
  CompiledWasmFunction& V8_EXPORT operator=(CompiledWasmFunction&& that);

  CompiledWasmFunction(CompiledWasmFunction const& that);
  CompiledWasmFunction& operator=(CompiledWasmFunction const& that);
  
  WasmType ReturnType() const;
  std::vector<WasmType> Parameters() const;

  std::string const& Name() const noexcept { return name; }

  V8_EXPORT ~CompiledWasmFunction();
  MaybeLocal<Value> V8_EXPORT Invoke(Isolate* i, std::vector<Local<Value>>& args) const;
  std::vector<uint8_t> V8_EXPORT Instructions() const;
};

class CompiledWasm {
  struct Internal;  
  std::vector<CompiledWasmFunction> functions;
  std::map<std::string, size_t> function_names;
  std::unique_ptr<Internal> internal;

  friend class CompiledWasmFunction;
  friend Maybe<CompiledWasm> v8::ext::CompileBinaryWasm(Isolate* i, const uint8_t* arr, size_t len);

  CompiledWasmFunction& AddOneFunction();
  void EnsureChildBinding();
public:
  struct WasmMemoryRef {
    std::shared_ptr<uint8_t> buffer;
    size_t length;
  };

  CompiledWasm();

  CompiledWasmFunction const& FunctionByIndex(size_t idx) { return functions[idx]; }
  CompiledWasmFunction const& FunctionByName(std::string const& name) { 
    return functions[function_names.at(name)]; 
  }

  CompiledWasmFunction const& operator[](size_t idx) { return functions[idx]; }
  CompiledWasmFunction const& operator[](std::string const& name) { return FunctionByName(name); }

  std::vector<CompiledWasmFunction> const& Functions() const noexcept { return functions; }

  V8_EXPORT CompiledWasm(CompiledWasm&& that);
  CompiledWasm& V8_EXPORT operator=(CompiledWasm&& that);

  V8_EXPORT CompiledWasm(CompiledWasm const& that);
  CompiledWasm& V8_EXPORT operator=(CompiledWasm const& that);

  bool V8_EXPORT InstantiateWasm(Isolate* i);

  void V8_EXPORT NewMemoryImport(v8::Isolate* i);

  WasmMemoryRef V8_EXPORT GetWasmMemory();

  size_t V8_EXPORT GetWasmMemorySize();

  V8_EXPORT ~CompiledWasm();
};

std::tuple<bool, size_t> V8_EXPORT GenerateRandomWasm(Isolate* i, std::vector<uint8_t> const& input, std::vector<uint8_t>& output);

} // namespace ext
} // namespace v8