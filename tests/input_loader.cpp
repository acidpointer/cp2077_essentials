#include "Merge.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using InputLoader::MergeBinding;
using InputLoader::MergeResult;

void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

size_t count(pugi::xml_node parent, const char *name) {
    size_t result = 0;
    for (auto node : parent.children(name)) {
        (void)node;
        ++result;
    }
    return result;
}

void parse(const std::filesystem::path &root) {
    pugi::xml_document document;
    pugi::xml_parse_result result;
    for (auto name : {"valid.xml", u8"unicode-інпут.xml"}) {
        require(InputLoader::ReadInputDocument(document, root / name, result), "valid input rejected");
        require(document.child("bindings"), "valid bindings missing");
    }
    for (auto name : {"malformed.xml", "wrong-root.xml", "empty.xml", "missing.xml"}) {
        require(!InputLoader::ReadInputDocument(document, root / name, result), "invalid input accepted");
        require(!document.first_child(), "failed input retained a partial tree");
    }
}

void filter(const std::filesystem::path &root) {
    for (auto name : {"valid.xml", "upper.XML", "mixed.XmL"})
        require(InputLoader::IsInputFile(std::filesystem::directory_entry(root / name)), "XML file skipped");
    for (auto name : {"text.xml.txt", "directory.xml"})
        require(!InputLoader::IsInputFile(std::filesystem::directory_entry(root / name)), "non-file/XML accepted");
}

void keyed() {
    pugi::xml_document contexts, mappings, mod;
    contexts.load_string("<bindings/>");
    mappings.load_string("<bindings><buttonGroup id='same' old='1'/><buttonGroup id='other'/></bindings>");
    mod.load_string("<bindings><buttonGroup id='same' new='1'/></bindings>");
    require(MergeBinding(contexts, mappings, mod.child("bindings").first_child()) == MergeResult::Replaced,
            "buttonGroup must match id");
    auto bindings = mappings.child("bindings");
    require(count(bindings, "buttonGroup") == 2, "buttonGroup duplicated or unrelated node removed");
    require(bindings.find_child_by_attribute("id", "same").attribute("new"), "buttonGroup replacement missing");
    for (auto type : {"hold", "multitap", "repeat", "toggle", "acceptedEvents"}) {
        contexts.reset();
        auto root = contexts.append_child("bindings");
        root.append_child(type).append_attribute("action") = "same";
        root.append_child(type).append_attribute("action") = "other";
        mod.reset();
        auto node = mod.append_child(type);
        node.append_attribute("action") = "same";
        node.append_attribute("new") = true;
        require(MergeBinding(contexts, mappings, node) == MergeResult::Replaced, "input block must match action");
        require(count(root, type) == 2, "action block duplicated or unrelated action removed");
        require(root.find_child_by_attribute("action", "same").attribute("new"), "action replacement missing");
    }
}

void append_replace() {
    pugi::xml_document contexts, mappings, mod;
    contexts.load_string("<bindings><context name='drive'><action name='old'/></context></bindings>");
    mappings.load_string("<bindings/>");
    mod.load_string("<context name='drive' append='true'><action name='new'/></context>");
    require(MergeBinding(contexts, mappings, mod.first_child()) == MergeResult::Appended, "append failed");
    auto root = contexts.child("bindings");
    require(count(root, "context") == 1 && count(root.child("context"), "action") == 2, "append lost children");
    mod.load_string("<context name='drive'><action name='replacement'/></context>");
    require(MergeBinding(contexts, mappings, mod.first_child()) == MergeResult::Replaced, "replace failed");
    require(count(root, "context") == 1 && count(root.child("context"), "action") == 1, "replace retained old children");
}

void routes() {
    pugi::xml_document contexts, mappings, mod;
    contexts.load_string("<bindings/>");
    mappings.load_string("<bindings/>");
    for (auto type : {"blend", "context", "hold", "multitap", "repeat", "toggle", "acceptedEvents"}) {
        mod.reset();
        require(MergeBinding(contexts, mappings, mod.append_child(type)) == MergeResult::Added, "context route failed");
        require(contexts.child("bindings").child(type) && !mappings.child("bindings").child(type), "wrong context route");
    }
    for (auto type : {"mapping", "buttonGroup", "pairedAxes", "preset"}) {
        mod.reset();
        require(MergeBinding(contexts, mappings, mod.append_child(type)) == MergeResult::Added, "mapping route failed");
        require(mappings.child("bindings").child(type) && !contexts.child("bindings").child(type), "wrong mapping route");
    }
    mod.load_string("<unsupported/>");
    require(MergeBinding(contexts, mappings, mod.first_child()) == MergeResult::Unsupported, "unknown type accepted");
    contexts.reset();
    mod.load_string("<context name='drive'/>");
    require(MergeBinding(contexts, mappings, mod.first_child()) == MergeResult::MissingBindings, "missing destination accepted");
}

void unkeyed() {
    pugi::xml_document contexts, mappings, mod;
    contexts.load_string("<bindings><blend/><context name=''/><hold action=''/></bindings>");
    mappings.load_string("<bindings/>");
    for (auto xml : {"<blend/>", "<context name=''/>", "<hold action=''/>", "<context/>"}) {
        mod.load_string(xml);
        require(MergeBinding(contexts, mappings, mod.first_child()) == MergeResult::Added, "empty key replaced a node");
    }
    require(count(contexts.child("bindings"), "blend") == 2, "blend must always append");
    require(count(contexts.child("bindings"), "context") == 3, "empty names must not collide");
}

int main(int argc, char **argv) {
    try {
        require(argc == 3, "expected case and fixture directory");
        const std::string name = argv[1];
        if (name == "parse") parse(argv[2]);
        else if (name == "filter") filter(argv[2]);
        else if (name == "keyed") keyed();
        else if (name == "append_replace") append_replace();
        else if (name == "routes") routes();
        else if (name == "unkeyed") unkeyed();
        else throw std::runtime_error("unknown case");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
