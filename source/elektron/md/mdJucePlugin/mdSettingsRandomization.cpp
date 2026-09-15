#include "mdSettingsRandomization.h"

#include "mdEditor.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/StringUtilities.h"

#include <algorithm>
#include <cmath>

namespace mdJucePlugin
{
	void SettingsRandomization::createUi(Rml::Element* _root)
	{
		auto* const slider = juceRmlUi::helper::findChild(_root, "sliderTrigChance", false);
		auto* const label = juceRmlUi::helper::findChild(_root, "labelTrigChance", false);
		if(!slider || !label)
			return;
		auto& config = m_processor.getConfig();
		const auto show = [label](const int _percent)
		{
			label->SetInnerRML(Rml::StringUtilities::EncodeRml(std::to_string(_percent) + " %"));
		};
		const auto current = std::clamp(config.getIntValue(Editor::g_trigChanceConfigKey, Editor::g_trigChanceDefault), 1, 99);
		slider->SetAttribute("value", std::to_string(current));
		show(current);
		juceRmlUi::EventListener::Add(slider, Rml::EventId::Change, [this, slider, &config, show](Rml::Event& _event)
		{
			_event.StopPropagation();
			const auto* const value = slider->GetAttribute("value");
			if(!value)
				return;
			const auto percent = std::clamp(static_cast<int>(std::lround(
				value->Get<float>(slider->GetCoreInstance()))), 1, 99);
			config.setValue(Editor::g_trigChanceConfigKey, percent);
			config.saveIfNeeded();
			show(percent);
			m_editor.applyScaleQuantizer();
		});
		juceRmlUi::EventListener::Add(slider, Rml::EventId::Dblclick, [this, slider, &config, show](Rml::Event& _event)
		{
			_event.StopPropagation();
			slider->SetAttribute("value", std::to_string(Editor::g_trigChanceDefault));
			config.setValue(Editor::g_trigChanceConfigKey, Editor::g_trigChanceDefault);
			config.saveIfNeeded();
			show(Editor::g_trigChanceDefault);
			m_editor.applyScaleQuantizer();
		});
	}
}
