#include "mdSettingsScale.h"

#include "mdEditor.h"
#include "mdLib/mdscale.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"

#include <algorithm>

namespace mdJucePlugin
{
	std::string SettingsScale::getTemplateName() const
	{
		return "tus_settings_scale_" + m_editor.getSettingsTemplateSuffix();
	}

	void SettingsScale::createUi(Rml::Element* _root)
	{
		auto& config = m_processor.getConfig();
		const auto bindCombo = [this, &config, _root](const char* _id, const char* _key,
			const std::vector<Rml::String>& _options)
		{
			auto* const combo = juceRmlUi::helper::findChildT<juceRmlUi::ElemComboBox>(_root, _id, false);
			if(!combo)
				return;
			combo->setOptions(_options);
			const auto current = std::clamp(config.getIntValue(_key, 0), 0, static_cast<int>(_options.size()) - 1);
			combo->setSelectedIndex(static_cast<size_t>(current), false);
			juceRmlUi::EventListener::Add(combo, Rml::EventId::Change, [this, &config, combo, _key](Rml::Event&)
			{
				config.setValue(_key, combo->getSelectedIndex());
				config.saveIfNeeded();
				m_editor.applyScaleQuantizer();
			});
		};
		std::vector<Rml::String> scales, roots;
		for(uint8_t i = 0; i < md::scale::g_scaleCount; ++i)
			scales.emplace_back(md::scale::scaleName(i));
		for(uint8_t i = 0; i < 12; ++i)
			roots.emplace_back(md::scale::rootName(i));
		bindCombo("cbScaleQuantizer", Editor::g_scaleConfigKey, scales);
		bindCombo("cbScaleRoot", Editor::g_scaleRootConfigKey, roots);
	}
}
