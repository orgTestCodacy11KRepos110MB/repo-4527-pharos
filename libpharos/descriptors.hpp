// Copyright 2015-2019 Carnegie Mellon University.  See LICENSE file for terms.

#ifndef Pharos_Descriptors_H
#define Pharos_Descriptors_H

#include <boost/iterator.hpp>
#include <rose.h>
#include <AstTraversal.h>
#include <Partitioner2/Engine.h>

namespace pharos {

using FCG = Rose::BinaryAnalysis::FunctionCall::Graph;

// Some forward declarations of our classes.

class PDG;
class spTracker;
class APIDictionary;

} // namespace pharos

#include "funcs.hpp"
#include "calls.hpp"
#include "imports.hpp"
#include "util.hpp"
#include "memory.hpp"
#include "globals.hpp"
#include "convention.hpp"
#include "options.hpp"
#include "misc.hpp"
#include "graph.hpp"

namespace pharos {

// For mapping the unique variable ids filled in by the loader back to the ImportDescriptor
// objects that describe those values.
using ImportVariableMap = std::map<size_t, ImportDescriptor*>;
using AddrInsnMap = std::map<rose_addr_t, SgAsmInstruction*>;

class DescriptorSet: public AstPreOrderTraversal
{
 private:
  CallDescriptorMap call_descriptors;
  FunctionDescriptorMap function_descriptors;
  ImportDescriptorMap import_descriptors;
  ImportVariableMap import_variables;
  GlobalMemoryDescriptorMap global_descriptors;
  CallingConventionMatcher calling_conventions;

  // These don't really belong here, but I want reading the program image to be globally
  // accessible, so this is the most convenient place for right now.
  SgAsmInterpretation *interp;

  // Arguments supplied to this descriptor set
  const ProgOptVarMap& vm;

  // The list of files to analyze
  std::vector<std::string> specimen_names;

  // The Function call graph of this program.
  FCG function_call_graph;

  // The new style instruction-level, whole-progam dependency graph.
  ProgramDependencyGraph pdg_graph;

  // A map of addresses to instructions.
  AddrInsnMap insn_map;

  // Architecture word size in bytes.
  size_t arch_bytes;
  // Architecture name from the disassembler.
  std::string arch_name;

  // I think that in the new Partitioner 2 world, it will be the most convenient to just have
  // access to the entire Partitioner 2 engine.  We'll eventually probably want to do things
  // like re-invoke the Partitioner to find additional code that might have been missed on the
  // first pass, or to eliminate things that were incorrect.  This pointer is usually a pointer
  // to a CERTEngine object, but it might be a pointer to a stock Partitioner engine (if the
  // user specified --stock, so some care is required when accessing custom extensions to the
  // class.  Hopefully we won't need to do that often.  This pointer is now entirely owned by
  // the descriptor set.
  P2::Engine* engine;

  // The partitioner is _created_ by the engine, but is not contained within it, so we have to
  // keep a copy of the partitioner in the descriptor set as well. Currently this is object is
  // locally allocated by the engine in create_partitioner() and copied into the object stored
  // here.
  P2::Partitioner partitioner;

  // A replacement for the global_rops concept?
  SymbolicRiscOperatorsPtr rops;

  void init();

  template <typename... Args>
  FunctionDescriptor *add_function_descriptor(rose_addr_t addr, Args &&... args);

  FunctionDescriptor *add_function_descriptor(SgAsmFunction *func) {
    return add_function_descriptor(func->get_entry_va(), func);
  }

  // Used to be public.  Could be again if needed.
  void update_connections();
  void validate(std::ostream &o);
  void preOrderVisit(SgNode* n);

  // Only used by update_import_target()
  ImportDescriptor* get_import_by_variable(SymbolicValuePtr v);

  // Add instructions to the instruction map (soon to be replaced with a Partitioner2
  // instruction provider).  Currently called only in preOrderVisit().
  void add_insn(rose_addr_t addr, SgAsmInstruction* insn);

  // Ensure that all imports in import_descriptors are also in import_variables.
  ImportDescriptor *add_import(rose_addr_t addr, std::string dll, std::string name, size_t ord = 0);


public:
  // Binary image
  Memory memory;

  // The API database.
  std::unique_ptr<APIDictionary> apidb;

  // This is the intended (standard) way to construct a descriptor set.
  DescriptorSet(const ProgOptVarMap& povm);
  // These are used if the filename(s) don't come from the program options
  DescriptorSet(const ProgOptVarMap& povm, std::vector<std::string> const & specimen_names);
  DescriptorSet(const ProgOptVarMap& povm, std::string const & specimen_name)
    : DescriptorSet(povm, std::vector<std::string>({specimen_name})) {}
  // Sadly tracesem.cpp wants to pass it in it's own engine and partitioner due to some
  // incosistencies in the way that we've munged together ROSE code and Pharos code.
  DescriptorSet(const ProgOptVarMap& povm, P2::Engine& eng, P2::Partitioner&& par);
  // Wes' indexer program does something very non-standard that requires a function instead.
  DescriptorSet(const ProgOptVarMap& povm, SgAsmFunction *func);

  ~DescriptorSet();

  // Load stack deltas for imports.
  // Public non-const method because it initiates entirely optional but significant work.
  void resolve_imports();

  // Used in one place in defuse.cpp to update locations that call imports.
  void update_import_target(SymbolicValuePtr& v, SgAsmX86Instruction* insn);

  // Only called from FunctionDescriptor::get_pdg() which is practically an honorary
  // method of defuse.cpp (where most of the other updates occur).
  void update_global_variables_for_func(const FunctionDescriptor* fd);

  // The AST must be non-const because the one place that uses this method (dumpmasm.cpp)
  // requires a mutable AST to call unparse.  Why unparse needs a mutable AST is not clear.
  // Bug in ROSE?  Once we're generating the AST dyanmically, it won't matter so much.
  SgAsmInterpretation* get_ast();

  // Only used in a non-const context in BottomUpAnalyzer?
  std::vector<FunctionDescriptor *> rw_funcs_in_bottom_up_order();

  // Mostly in calls.cpp for updating callers, set_delete_method() in ooanalyzer.cpp
  FunctionDescriptor* get_rw_func(rose_addr_t a) {
    return function_descriptors.get_func(a);
  }
  // Used to call add_import_target() twice, major analysis updates in defuse.cpp
  CallDescriptor* get_rw_call(rose_addr_t a) {
    return call_descriptors.get_call(a);
  }
  // Used for call descriptor creation in calls.cpp, one to add virtualized targets
  // One reference in readMemory() in riscop.cpp, to add calls to import memory refs
  ImportDescriptor* get_rw_import(rose_addr_t a) {
    return import_descriptors.get_import(a);
  }
  // Used to add_ref() in descriptors.cpp, add_read() and add_write() in defuse.cpp
  GlobalMemoryDescriptor* get_rw_global(rose_addr_t a) {
    return global_descriptors.get_global(a);
  }

  //======================================================================================
  // Const interface
  //======================================================================================

  const FunctionDescriptor* get_func(rose_addr_t a) const {
    return function_descriptors.get_func(a);
  }
  const CallDescriptor* get_call(rose_addr_t a) const {
    return call_descriptors.get_call(a);
  }
  const ImportDescriptor* get_import(rose_addr_t a) const {
    return import_descriptors.get_import(a);
  }
  const GlobalMemoryDescriptor* get_global(rose_addr_t a) const {
    return global_descriptors.get_global(a);
  }

  const ProgramDependencyGraph& get_new_pdg_graph() { return pdg_graph; }

  // Const only access directly to the maps (for enumerating descriptors).
  // Access directly to the maps.
  const FunctionDescriptorMap& get_func_map() const { return function_descriptors; }
  const CallDescriptorMap& get_call_map() const { return call_descriptors; }
  const ImportDescriptorMap& get_import_map() const { return import_descriptors; }
  const GlobalMemoryDescriptorMap& get_global_map() const { return global_descriptors; }

  // Find the function descriptor that contains a given instruction.
  const FunctionDescriptor* get_fd_from_insn(const SgAsmInstruction *insn) const;
  // Used to be in path.cpp, but is essentially a descriptor set responsibility.
  const FunctionDescriptor* get_func_containing_address(rose_addr_t addr) const;
  SgAsmBlock* get_block_containing_address (rose_addr_t addr) const {
    return insn_get_block(get_insn(addr));
  }
  // A newer interface to finding blocks based on addresses?
  const P2::BasicBlock::Ptr get_block(rose_addr_t a) const {
    return partitioner.basicBlockContainingInstruction(a);
  }

  Rose::BinaryAnalysis::Disassembler* get_disassembler() const {
    // We're const casting here because the disassembler that we return has no ability to
    // modify anything of importance in the descriptor set.  Arguably, the obtainDisassembler
    // method ought to be const on the engine as well.
    return const_cast<DescriptorSet*>(this)->engine->obtainDisassembler();
  }

  void dump(std::ostream &o) const;

  const ProgOptVarMap& get_arguments() const { return vm; }

  const CallingConventionMatcher& get_calling_conventions() const { return calling_conventions; }

  const FCG& get_function_call_graph() const { return function_call_graph; }
  std::vector<const FunctionDescriptor *> const_funcs_in_bottom_up_order() const;

  // Here's how you can get access to the new Partitioner 2 engine (maybe)...
  const P2::Engine* get_engine() const { return engine; }
  const P2::Partitioner& get_partitioner() const { return partitioner; }
  const RegisterDictionary get_regdict() const;

  // And this should really be on an instruction provider (from P2::Partitioner).
  SgAsmInstruction* get_insn(rose_addr_t addr) const;

  // Return the default word size on the architecture.
  const std::string& get_arch_name() const { return arch_name; }
  size_t get_arch_bytes() const { return arch_bytes; }
  size_t get_arch_bits() const { return arch_bytes * 8; }

  // Find a general purpose register in an semi-architecture independent way.
  RegisterDescriptor get_arch_reg(const std::string & name) const;
  // Find the stack pointer register in an architecture independent way.
  RegisterDescriptor get_stack_reg() const {
    return partitioner.instructionProvider().stackPointerRegister();
  }
  // Find the instruction pointer register in an architecture independent way.
  RegisterDescriptor get_ip_reg() const {
    return partitioner.instructionProvider().instructionPointerRegister();
  }

  // Return how many threads to use during processing
  unsigned int get_concurrency_level() const {
    return get_concurrency_level(vm);
  }
  static unsigned int get_concurrency_level(ProgOptVarMap const & vm);
};

extern size_t global_arch_bytes;

} // namespace pharos

#endif
/* Local Variables:   */
/* mode: c++          */
/* fill-column:    95 */
/* comment-column: 0  */
/* End:               */
