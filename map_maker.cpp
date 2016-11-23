// g++ -std=c++11 map_maker.cpp -o map_maker -lclangAST -lclangASTMatchers -lclangFrontend -lclangBasic -lLLVMSupport -lclangTooling
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <llvm/Support/CommandLine.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <fstream>

// see AST matcher reference for available macros at http://clang.llvm.org/docs/LibASTMatchersReference.html
using namespace clang::ast_matchers;

auto DOFieldMatcher = fieldDecl(isPublic(), hasType(recordDecl(hasName("DataObject")))).bind("DO");
auto LinkFieldMatcher = fieldDecl(isPublic(), hasType(recordDecl(hasName("Link")))).bind("link");
auto ModuleClassMatcher = cxxRecordDecl(hasMethod(hasName("deserialize")), hasMethod(hasName("serialize")), anyOf(has(DOFieldMatcher), has(LinkFieldMatcher))).bind("moduleClass");
auto ModuleInstanceMatcher = varDecl(hasType(ModuleClassMatcher)).bind("moduleInstance");

std::unordered_multimap<std::string, std::string> DOs; // key: class, value: DO
std::unordered_multimap<std::string, std::string> Links; // key: class, value: link
std::unordered_map<std::string, std::string> ModuleInstances; // key: variable, value: class

class MapMaker : public clang::ast_matchers::MatchFinder::MatchCallback
{
public:
	void run(const clang::ast_matchers::MatchFinder::MatchResult &Result) override
	{
		if (const clang::RecordDecl *rd = Result.Nodes.getNodeAs<clang::RecordDecl>("moduleClass"))
		{
			std::string moduleClass = rd->getNameAsString();
			if (const clang::VarDecl *vd = Result.Nodes.getNodeAs<clang::VarDecl>("moduleInstance")) {
				ModuleInstances.emplace(vd->getNameAsString(), moduleClass);
			}
		}
		else if (const clang::FieldDecl *fd = Result.Nodes.getNodeAs<clang::FieldDecl>("DO")) {
			DOs.emplace(fd->getParent()->getNameAsString(), fd->getNameAsString());
		}
		else if (const clang::FieldDecl *fd = Result.Nodes.getNodeAs<clang::FieldDecl>("link")) {
			Links.emplace(fd->getParent()->getNameAsString(), fd->getNameAsString());
		}
	}
};

static llvm::cl::OptionCategory MapMakerCategory("MapMaker options");
//static llvm::cl::extrahelp CommonHelp(clang::tooling::CommonOptionsParser::HelpMessage);

int main(int argc, const char **argv)
{
	clang::tooling::CommonOptionsParser OptionsParser(argc, argv, MapMakerCategory);
	clang::tooling::ClangTool Tool(OptionsParser.getCompilations(), OptionsParser.getSourcePathList());

	MapMaker Maker;
	clang::ast_matchers::MatchFinder Finder;
	Finder.addMatcher(DOFieldMatcher, &Maker);
	Finder.addMatcher(LinkFieldMatcher, &Maker);
	Finder.addMatcher(ModuleInstanceMatcher, &Maker);

	int Result = Tool.run(clang::tooling::newFrontendActionFactory(&Finder).get());

	std::fstream fs;

	fs.open("global_reflection.h", std::fstream::out);

	fs << "#pragma once" << std::endl
		<< "#include <unordered_map>" << std::endl
		<< "#include <functional>" << std::endl
		<< std::endl
		<< "#include <boost/any.hpp>" << std::endl
		<< std::endl
		<< "using module_name_map = std::unordered_map<void*, std::string>;" << std::endl
		<< "using name_module_map = std::unordered_map<std::string, boost::any>;" << std::endl
		<< "using dataobject_name_map = std::unordered_map<void*, std::string>;" << std::endl
		<< "using name_dataobject_map = std::unordered_map<std::string, boost::any>;" << std::endl
		<< "using doset_map = std::unordered_map<std::string, std::function<void(const boost::any)>>;" << std::endl
		<< "using registerlink_map = std::unordered_map<std::string, std::function<void(std::string, boost::any, boost::any)>>;" << std::endl
		<< "using unregisterlink_map = std::unordered_map<std::string, std::function<void(std::string, boost::any)>>;" << std::endl
		<< std::endl
		<< "extern const module_name_map module_names;" << std::endl
		<< "extern const name_module_map modules;" << std::endl
		<< "extern const dataobject_name_map do_names;" << std::endl
		<< "extern const name_dataobject_map dos;" << std::endl
		<< "extern const doset_map do_set;" << std::endl
		<< "extern const registerlink_map set_links;" << std::endl
		<< "extern const unregisterlink_map clear_links;" << std::endl;

	fs.close();



	fs.open("global_reflection.cpp", std::fstream::out);

	fs << "#include \"global_reflection.h\"" << std::endl << std::endl;
	fs << "const name_module_map modules" << std::endl << "{" << std::endl;
	for (auto &ModuleInstance : ModuleInstances)
	{
		fs << "\t{\"" + ModuleInstance.second + "." + ModuleInstance.first + "\", &" + ModuleInstance.first + "}";
		fs << ",";
		fs << std::endl;
	}
	fs << "};" << std::endl << std::endl;

	fs << "// Get out the module name for humans" << std::endl;
	fs << "const module_name_map module_names" << std::endl << "{" << std::endl;
	for (auto &ModuleInstance : ModuleInstances)
	{
		fs << "\t{&" + ModuleInstance.first + ", \"" + ModuleInstance.second + "." + ModuleInstance.first + "\"}";
		fs << ",";
		fs << std::endl;
	}
	fs << "};" << std::endl << std::endl;

	fs << "const name_dataobject_map dos" << std::endl << "{" << std::endl;
	for (auto &ModuleInstance : ModuleInstances)
	{
		auto range = DOs.equal_range(ModuleInstance.second);
		for (auto it = range.first; it != range.second; ++it)
		{
			fs << "\t{\"" + ModuleInstance.second + "." + ModuleInstance.first + "." + it->second + "\", &" + ModuleInstance.first + "." + it->second + "}," << std::endl;
		}
	}
	fs << "};" << std::endl << std::endl;

	fs << "// Get out the DO name for humans" << std::endl;
	fs << "const dataobject_name_map do_names" << std::endl << "{" << std::endl;
	for (auto &ModuleInstance : ModuleInstances)
	{
		auto range = DOs.equal_range(ModuleInstance.second);
		for (auto it = range.first; it != range.second; ++it)
		{
			fs << "\t{&" + ModuleInstance.first + "." + it->second + ", \"" + ModuleInstance.second + "." + ModuleInstance.first + "." + it->second + "\"}," << std::endl;
		}
	}
	fs << "};" << std::endl << std::endl;

	fs << "const doset_map do_set" << std::endl << "{" << std::endl;;

	for (auto &ModuleInstance : ModuleInstances)
	{
		auto range = DOs.equal_range(ModuleInstance.second);
		for (auto it = range.first; it != range.second; ++it)
		{
			fs << "\t{\"" + ModuleInstance.second + "." + ModuleInstance.first + "." + it->second + "\", [](const boost::any &a) {" + ModuleInstance.first + "." + it->second + ".set(a); } }," << std::endl;
		}
	}
	fs << "};" << std::endl << std::endl;

	fs << "const registerlink_map set_links" << std::endl << "{" << std::endl;
	for (auto &ModuleInstance : ModuleInstances)
	{
		auto range = Links.equal_range(ModuleInstance.second);
		for (auto it = range.first; it != range.second; ++it)
		{
			fs << "\t{\"" + ModuleInstance.second + "." + ModuleInstance.first + "." + it->second + "\", [](std::string name, boost::any a1, boost::any a2) {" + ModuleInstance.first + "." + it->second + ".set(name, a1, a2); } }," << std::endl;
		}
	}
	fs << "};" << std::endl << std::endl;


	fs << "const unregisterlink_map clear_links" << std::endl << "{" << std::endl;;
	for (auto &ModuleInstance : ModuleInstances)
	{
		auto range = Links.equal_range(ModuleInstance.second);
		for (auto it = range.first; it != range.second; ++it)
		{
			fs << "\t{\"" + ModuleInstance.second + "." + ModuleInstance.first + "." + it->second + "\", [](std::string name, boost::any a) {" + ModuleInstance.first + "." + it->second + ".clear(name, a); } }," << std::endl;
		}
	}
	fs << "};" << std::endl << std::endl;

	fs.close();


	return Result;
}
