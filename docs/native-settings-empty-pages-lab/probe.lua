-- Execute the installed library, replacing only engine-facing objects.
-- This checks the actual page-population callback and Cron scheduling.
package.path = inputDirectory .. "/?.lua;" .. package.path
local hooks, events = {}, {}
function registerForEvent(name, callback) events[name] = callback end
function Override(class, name, callback) hooks[class .. "." .. name] = callback end
function Observe() end
function ObserveAfter() end
function GetVersion() return "v1.38.0" end
CName = { add = function() end }
function StringToName(s) return {value = s} end
function NameToString(n) return n.value end
function IsDefined(value) return value ~= nil end
SettingsCategory = { new = function()
    local values = {subcategories = {}}
    return setmetatable({}, {
        __index = values,
        __newindex = function(_, k, v)
            if k == "label" or k == "groupPath" then v = StringToName(v) end
            values[k] = v
        end
    })
end }
local ns = dofile(inputDirectory .. "/init.lua")
events.onInit()
local function noop() end
local trace = {cleared = 0, headings = 0, controls = 0}
local controller = {RegisterToCallback = noop, Setup = noop}
controller.LabelText = {SetText = noop}
controller.ValueText = {SetText = noop}
controller.onState = {SetVisible = noop}
controller.offState = {SetVisible = noop}
controller.sliderWidget = {GetControllerByType = function() return controller end}
local scroll = {
    GetVerticalScrollPosition = function() return 0 end,
    GetContentSize = function() return {Y = 100} end,
    GetController = function() return {SetScrollPosition = noop} end,
}
local owner = {
    descriptionText = descriptionReference,
    settingsElements = {},
    settingsOptionsList = {widget = {}, RemoveAllChildren = function()
        trace.cleared = trace.cleared + 1
        trace.headings, trace.controls = 0, 0
    end},
    groupsSelectorCtrl = {GetToggledIndex = function() return 0 end, SetSelectedIndex = noop},
    GetRootWidget = function() return {GetWidget = function() return scroll end} end,
    SpawnFromLocal = function(_, _, name)
        if name == "settingsCategory" then trace.headings = trace.headings + 1
        else trace.controls = trace.controls + 1 end
        return {SetName = noop, GetController = function() return controller end}
    end,
}
owner.PopulateSettingsData = function(self)
    hooks["SettingsMainGameController.PopulateSettingsData"](self, noop)
end
-- Representative tabs through the real public registration API. Individual
-- mod startup/gameplay logic is outside the shared rendering failure.
local labels = {"Deceptious Quest Core", "General Vehicle Fixes", "Better Flashlight"}
for i, label in ipairs(labels) do
    local path = "/probe" .. i
    ns.addTab(path, label)
    ns.addSubcategory(path .. "/settings", "Settings")
    ns.addSwitch(path .. "/settings", "Enabled", "Probe", true, true, noop)
    ns.addRangeFloat(path .. "/settings", "Value", "Probe", 0, 10, 1, "%.1f", 1, 1, noop)
end
ns.fromMods = true
ns.tabSizeCache = {labels}
ns.settingsMainController = owner
ns.settingsOptionsList = owner.settingsOptionsList
for index = 0, 2 do
    local ok, errorMessage = pcall(hooks["SettingsMainGameController.PopulateCategorySettingsOptions"], owner, index, noop)
    if ok then
        for _ = 1, 3 do events.onUpdate(0.016) end
    end
    print(string.format("page=%d tabs=%d cleared=%d headings=%d controls=%d success=%s",
        index, #owner.data, trace.cleared, trace.headings, trace.controls, tostring(ok)))
    if not ok then print(errorMessage) end
    if ancestryMode then
        assert(ok and trace.headings == 1 and trace.controls == 2)
    else
        assert(not ok and tostring(errorMessage):find("parameter 1 must be inkWidgetReference", 1, true))
        assert(trace.cleared == index + 1 and trace.headings == 0 and trace.controls == 0)
    end
end
print("PASS: expected conversion and page-population outcome; no game execution")
