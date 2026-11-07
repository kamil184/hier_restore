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
		log("from. Run it after synthesis; the design must have been flattened with\n");
		log("$scopeinfo cells, which 'flatten' and the synth_* scripts create by default.\n");
		log("The netlist itself is not changed, only these attributes are set:\n");
		log("\n");
		log("    hier_scope        on every cell: the instance path, written like 'hdlname'\n");
		log("                      (instance names separated by spaces, empty for the top)\n");
		log("    hier_status       on every cell, one of:\n");
		log("                        unique     the source location and the connected nets\n");
		log("                                   point to exactly one instance\n");
		log("                        inferred   no source location; placed by the connected\n");
		log("                                   nets, or by the cell name if no net is known\n");
		log("                        merged     synthesis combined logic of several\n");
		log("                                   instances into this cell\n");
		log("                        ambiguous  the cell comes from one of several\n");
		log("                                   instances, which one is unknown\n");
		log("                        unknown    no usable information, placed in the top\n");
		log("                      merged and ambiguous cells are placed in the innermost\n");
		log("                      instance that contains all candidates (see -inside).\n");
		log("    hier_candidates   on merged and ambiguous cells: the instance paths\n");
		log("                      involved, separated by '&' on merged cells (all of\n");
		log("                      them) and by '|' on ambiguous cells (one of them)\n");
		log("    hier_evidence     on every cell: what the result is based on, a comma\n");
		log("                      separated subset of hdlname, src, net, name\n");
		log("    hier_scope        on every wire: the instance its name belongs to, or the\n");
		log("                      innermost instance that contains all connected cells\n");
		log("    hier_status       on every $scopeinfo cell, one of:\n");
		log("                        complete   cells were placed here, none is shared\n");
		log("                        partial    some logic was merged with other instances\n");
		log("                        absorbed   all known logic was merged into other cells\n");
		log("                        empty      no cell left, the logic was optimized away\n");
		log("                                   or merged without a trace\n");
		log("\n");
		log("    -inside\n");
		log("        place merged and ambiguous cells in one of their candidates instead of\n");
		log("        the instance containing all of them, so that no cell leaves the\n");
		log("        instances it came from: the candidate the source location points to,\n");
		log("        else the innermost one, else the one the cell name points to, else\n");
		log("        the first by path. hier_candidates still lists all of them, and the\n");
		log("        status of an instance counts these cells as its own.\n");
		log("\n");
		log("    -o <file>\n");
		log("        also write the result as a JSON report\n");
		log("\n");
		log("    -sep <sep>\n");
		log("        hierarchy separator used by flatten (default: the value of the\n");
		log("        'flatten.separator' scratchpad variable, or '.')\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		log_header(design, "Executing HIER_RESTORE pass.\n");

		std::string sep = design->scratchpad_get_string("flatten.separator", "."), outfile;
		bool inside = false;
		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			if (args[argidx] == "-sep" && argidx + 1 < args.size()) {
				sep = args[++argidx];
				continue;
			}
			if (args[argidx] == "-inside") {
				inside = true;
				continue;
			}
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

		HierRestoreWorker worker(top, sep, inside);
		worker.run();
		worker.log_summary();
		if (!outfile.empty())
			worker.write_report(outfile);
	}
} HierRestorePass;

PRIVATE_NAMESPACE_END
