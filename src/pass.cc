#include "worker.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

using hier_restore::HierRestoreWorker;

struct HierRestorePass : public Pass {
	HierRestorePass() : Pass("hier_restore", "recover the module hierarchy of a synthesized, flattened design") {}

	void help() override
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    hier_restore [options]\n");
		log("\n");
		log("Assigns every cell of the flattened top module to the module instance it came\n");
		log("from. Run it after synthesis. The instances are taken from the $scopeinfo cells\n");
		log("that 'flatten' and the synth_* scripts create by default. synth_gatemate removes\n");
		log("them in its last step, so run it with '-run :vlog' to stop before that step.\n");
		log("The hierarchy separator is the one flatten used ('flatten.separator').\n");
		log("The netlist itself is not changed, only these attributes are set:\n");
		log("\n");
		log("    hier_scope        on every cell: the instance path, written like 'hdlname'\n");
		log("                      (instance names separated by spaces, empty for the top)\n");
		log("    hier_status       on every cell, one of:\n");
		log("                        unique     the cell comes from exactly one instance\n");
		log("                        ambiguous  the cell comes from one or more of several\n");
		log("                                   instances, because synthesis merged their\n");
		log("                                   logic or because nothing tells them apart;\n");
		log("                                   it is placed in the innermost instance that\n");
		log("                                   contains all of them\n");
		log("                        unknown    nothing tells where the cell comes from,\n");
		log("                                   it is placed in the top module\n");
		log("    hier_scope        on every wire: the instance its name belongs to, or the\n");
		log("                      innermost instance that contains all connected cells\n");
		log("\n");
		log("    -o <file>\n");
		log("        also write the result as a JSON report\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		log_header(design, "Executing HIER_RESTORE pass.\n");

		std::string sep = design->scratchpad_get_string("flatten.separator", "."), outfile;
		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			if (args[argidx] == "-o" && argidx + 1 < args.size()) {
				outfile = args[++argidx];
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);

		if (sep.empty())
			log_cmd_error("The hierarchy separator must not be empty.\n");
		RTLIL::Module *top = design->top_module();
		if (top == nullptr)
			log_cmd_error("No top module found.\n");

		HierRestoreWorker worker(top, sep);
		worker.run();
		worker.log_summary();
		if (!outfile.empty())
			worker.write_report(outfile);
	}
} HierRestorePass;

PRIVATE_NAMESPACE_END
