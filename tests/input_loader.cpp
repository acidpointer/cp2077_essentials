#include "Merge.hpp"
#include "Store.hpp"
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <cstring>

using namespace InputLoader;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
size_t count(pugi::xml_node parent, const char* name) { size_t n = 0; for (auto child : parent.children(name)) { (void)child; ++n; } return n; }
const std::string C = "<bindings><context name='Exploration'><action name='Jump' map='Jump_Button'/></context></bindings>";
const std::string M = "<bindings><mapping name='Jump_Button' type='Button'><button id='IK_Space'/></mapping></bindings>";
const std::string C2 = "<bindings><context name='Exploration'><action name='Jump' map='Jump_Button'/><action name='New' map='Jump_Button'/></context></bindings>";
const fs::path Root = "/game";
const fs::path Ini = Root / "engine/config/platform/pc/input_loader.ini";
const std::string Scope = "game-2-5-0";

struct Memory : Storage {
  std::map<fs::path, std::string> files;
  std::set<fs::path> directories;
  int writes = 0, commits = 0, nonce = 0, failWrite = 0, failCommit = 0;
  bool afterCommit = false;
  bool Exists(const fs::path& p) override { return files.count(p) || directories.count(p); }
  std::string Read(const fs::path& p) override { if (!files.count(p)) throw std::runtime_error("read failed"); return files.at(p); }
  void Directories(const fs::path& p) override { directories.insert(p); }
  void Write(const fs::path& p, const std::string& text) override {
    require(!files.count(p), "must never overwrite a staged/generation file");
    if (++writes == failWrite) { files[p] = text.substr(0, text.size()/2); throw std::runtime_error("write/flush/close failed"); }
    files[p] = text;
  }
  void Replace(const fs::path& from, const fs::path& to) override {
    const bool fail = ++commits == failCommit;
    if (fail && !afterCommit) throw std::runtime_error("commit denied");
    files[to] = files.at(from); files.erase(from);
    if (fail) throw std::runtime_error("late commit status error");
  }
  std::string Unique() override { return std::string(24,'0') + std::to_string(++nonce); }
};
void CheckSelection(Memory& io, const std::string& expectedC, const std::string& expectedM) {
  const auto paths = ReadSelector(io.Read(Ini));
  require(paths && paths->managed, "must select one managed generation");
  require(io.Read(Root/"r6"/RelativeCachePath(paths->contexts)) == expectedC, "wrong context generation");
  require(io.Read(Root/"r6"/RelativeCachePath(paths->mappings)) == expectedM, "wrong mappings generation");
}
void parse(const fs::path& root) {
  pugi::xml_document doc; pugi::xml_parse_result result;
  for (auto name : {"valid.xml", u8"unicode-інпут.xml"}) require(ReadInputDocument(doc,root/name,result), "valid input rejected");
  for (auto name : {"malformed.xml","wrong-root.xml","empty.xml","missing.xml","multiple.xml"}) {
    require(!ReadInputDocument(doc,root/name,result), "invalid document accepted");
    require(!doc.first_child(), "invalid parse retained a partial document");
  }
  pugi::xml_document c,m;c.load_string("<bindings/>");m.load_string(M.c_str());require(!ValidPair(c,m),"empty base accepted");
  for (const auto text : {"garbage<bindings/>", "<bindings/>garbage", "<![CDATA[x]]><bindings/>"})
    require(!ParseDocument(doc,text), "out-of-root content accepted");
  c.load_string(C.c_str());m.load_string("<bindings><mapping name='x' append='true'/></bindings>");
  require(!ValidPair(c,m), "incomplete final mapping disguised as an append operation");
}
void filter(const fs::path& root) {
  for (auto name : {"valid.xml","upper.XML","mixed.XmL"}) require(IsInputFile(fs::directory_entry(root/name)),"XML skipped");
  for (auto name : {"text.xml.txt","directory.xml"}) require(!IsInputFile(fs::directory_entry(root/name)),"non-XML/file accepted");
}
void keys() {
  for (auto kind : {"context","mapping","pairedAxes","preset","buttonGroup","hold","multitap","repeat","toggle","acceptedEvents","transition"}) {
    pugi::xml_document c,m,mod;c.load_string(C.c_str());m.load_string(M.c_str());
    const char* key = KeyAttribute(kind); auto root = (IsContextType(kind)?c:m).child("bindings");
    auto old=root.append_child(kind); old.append_attribute(key)="same";
    if(std::string_view(kind)=="mapping")old.append_attribute("type")="Button";
    mod.reset();auto node=mod.append_child(kind);node.append_attribute(key)="same";node.append_attribute("new")="yes";
    if(std::string_view(kind)=="mapping")node.append_attribute("type")="Button";
    require(MergeBinding(c,m,node)==MergeResult::Replaced,"keyed replacement failed");
    require(root.find_child_by_attribute(kind,key,"same").attribute("new"),"replacement missing");
  }
}
void append_replace() {
  pugi::xml_document c,m,mod;c.load_string(C.c_str());m.load_string(M.c_str());
  mod.load_string("<context name='Exploration' append='true'><action name='new'/></context>");
  require(MergeBinding(c,m,mod.first_child())==MergeResult::Appended,"append failed");
  require(count(c.child("bindings").child("context"),"action")==2,"children lost");
  mod.load_string("<context name='Exploration'><action name='replacement'/></context>");
  require(MergeBinding(c,m,mod.first_child())==MergeResult::Replaced,"replace failed");
  require(count(c.child("bindings").child("context"),"action")==1,"old children retained");
}
void validation() {
  pugi::xml_document c,m,mod;c.load_string(C.c_str());m.load_string(M.c_str());
  for(auto xml:{"<context/>","<context name=''/>","<hold action=''/>","<mapping name='x'/>","<blend/>","<buttonGroup/>"}){
    mod.load_string(xml);require(MergeBinding(c,m,mod.first_child())==MergeResult::Invalid,"missing attributes accepted");
  }
  mod.load_string("<unknown/>");require(MergeBinding(c,m,mod.first_child())==MergeResult::Unsupported,"unknown type accepted");
  mod.load_string("<blend from='a' to='b' event='NONE'/>");
  require(MergeBinding(c,m,mod.first_child())==MergeResult::Added,"unkeyed blend rejected");
  require(MergeBinding(c,m,mod.first_child())==MergeResult::Added,"distinct blend application should append");
  c.reset();mod.load_string("<context name='x'/>");require(MergeBinding(c,m,mod.first_child())==MergeResult::MissingBindings,"missing root accepted");
}
void duplicates() {
  pugi::xml_document c,m,mod;c.load_string("<bindings><context name='Exploration'/><acceptedEvents action='x'><event name='old'/></acceptedEvents><acceptedEvents action='x'><event name='old'/></acceptedEvents></bindings>");m.load_string(M.c_str());
  mod.load_string("<acceptedEvents action='x' append='true'><event name='new'/></acceptedEvents>");MergeBinding(c,m,mod.first_child());
  for(auto node:c.child("bindings").children("acceptedEvents"))require(count(node,"event")==2,"append did not update every explicit key match");
  mod.load_string("<acceptedEvents action='x'><event name='replacement'/></acceptedEvents>");MergeBinding(c,m,mod.first_child());
  require(count(c.child("bindings"),"acceptedEvents")==1,"explicit replacement left an ambiguous old match");
}
void* fail_alloc(size_t){return nullptr;}
void oom() {
  pugi::xml_document base,m,mod;base.load_string(C.c_str());m.load_string(M.c_str());
  for (const auto& text : {
      "<context name='Exploration' payload='"+std::string(200000,'x')+"'/>",
      "<context name='Exploration' "+std::string(200000,'x')+"='value'/>"}) {
    mod.load_string(text.c_str());
    pugi::xml_document candidate, candidateM;CopyDocumentChecked(candidate,base);CopyDocumentChecked(candidateM,m);
    auto alloc=pugi::get_memory_allocation_function();auto free=pugi::get_memory_deallocation_function();
    pugi::set_memory_management_functions(fail_alloc,free);bool threw=false;
    try{MergeBinding(candidate,candidateM,mod.first_child());}catch(const std::bad_alloc&){threw=true;}
    pugi::set_memory_management_functions(alloc,free);
    require(threw,"partial copy reported success");require(Serialize(base).find("Jump")!=std::string::npos,"immutable baseline was mutated");
  }
}
void recovery() {
  Memory io;io.files[Ini]="[Player/Input]\nInputContextFile = \"cache\\inputContexts.xml\"\nInputMappingFile = \"cache\\inputUserMappings.xml\"";
  Store store(io,Root,Scope);store.Recover();require(io.Read(Ini)==NeutralIni,"unsafe legacy selector was not neutralized");
  store.Publish(C,M,std::string(64,'a'));CheckSelection(io,C,M);
  io.files[Ini]=NeutralIni;Store restarted(io,Root,Scope);restarted.Recover();CheckSelection(io,C,M);
  Store otherVersion(io,Root,"game-2-6-0");otherVersion.Recover();require(io.Read(Ini)==NeutralIni,"restored another runtime's generation");
  io.files[Ini]=ManagedMarker+"[Player/Input]\nInputContextFile=broken";
  Store corrupt(io,Root,"game-2-6-0");corrupt.Recover();require(io.Read(Ini)==NeutralIni,"corrupt owned selector was mistaken for custom content");
  io.files[Ini]="[Player/Input]\nCustom = value\n";Store custom(io,Root,Scope);bool rejected=false;
  try{custom.Recover();}catch(...){rejected=true;}require(rejected && io.Read(Ini).find("Custom")!=std::string::npos,"custom INI overwritten");
}
void publication() {
  Memory original;Store initial(original,Root,Scope);initial.Recover();initial.Publish(C,M,std::string(64,'a'));CheckSelection(original,C,M);
  const auto old=original.Read(Ini);
  for(int failure=1;failure<=4;++failure){
    auto io=original;Store store(io,Root,Scope);store.Recover();io.failWrite=io.writes+failure;bool rejected=false;
    try{store.Publish(C2,M,std::string(64,'b'));}catch(...){rejected=true;}
    if(rejected){require(io.Read(Ini)==old,"failed write changed selector");CheckSelection(io,C,M);}else CheckSelection(io,C2,M);
  }
  auto denied=original;Store fail(denied,Root,Scope);fail.Recover();denied.failCommit=denied.commits+1;bool rejected=false;
  try{fail.Publish(C2,M,std::string(64,'b'));}catch(...){rejected=true;}require(rejected,"denied commit reported success");CheckSelection(denied,C,M);
  auto late=original;Store committed(late,Root,Scope);committed.Recover();late.failCommit=late.commits+1;late.afterCommit=true;
  committed.Publish(C2,M,std::string(64,'b'));CheckSelection(late,C2,M);
}
void reuse() {
  Memory io;Store store(io,Root,Scope);store.Recover();store.Publish(C,M,std::string(64,'a'));
  auto writes=io.writes;require(!store.Publish(C,M,std::string(64,'a')) && io.writes==writes,"unchanged generation rewritten");
  store.Publish(C2,M,std::string(64,'b'));auto current=io.Read(Ini);
  // An abandoned/corrupt directory may never be repaired in place.
  io.directories.insert(Root/"r6/cache/input_loader"/Scope/std::string(64,'c'));
  io.files[Root/"r6/cache/input_loader"/Scope/std::string(64,'c')/"inputContexts.xml"]="partial";
  store.Publish(C,M,std::string(64,'c'));CheckSelection(io,C,M);require(io.Read(Ini)!=current,"new generation not selected");
}
int main(int argc,char**argv){try{
  require(argc==3,"expected case and fixture directory");const std::string mode=argv[1];
  if(mode=="parse")parse(argv[2]);else if(mode=="filter")filter(argv[2]);else if(mode=="keyed")keys();
  else if(mode=="append_replace")append_replace();else if(mode=="validation")validation();else if(mode=="duplicates")duplicates();
  else if(mode=="oom")oom();else if(mode=="recovery")recovery();else if(mode=="publication")publication();else if(mode=="reuse")reuse();
  else throw std::runtime_error("unknown case");
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
