#include "kernel/yosys.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

// Prints every module and the $scopeinfo cells it contains.
struct HierDumpPass : public Pass {
	HierDumpPass() : Pass("hier_dump", "print modules and scope information") {}

	void help() override
	{
		log("\n");
		log("    hier_dump\n");
		log("\n");
		log("Prints every module of the current design and its $scopeinfo cells.\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		extra_args(args, 1, design);

		for (auto module : design->modules()) {
			log("module %s%s\n", log_id(module), module->get_bool_attribute(ID::top) ? " (top)" : "");
			for (auto cell : module->cells())
				if (cell->type == ID($scopeinfo))
					log("  scope %s -> %s\n", log_id(cell), cell->get_string_attribute(ID::hdlname).c_str());
		}
	}
} HierDumpPass;

PRIVATE_NAMESPACE_END
