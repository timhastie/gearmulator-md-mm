#include "mdSettingsPanelFeel.h"

#include "mdEditor.h"
#include "mdLcdInteractionModel.h"
#include "mdPixelPerfectPanel.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "jucePluginEditorLib/settingsPlugin.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "mdLib/mdscale.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"

#include <algorithm>

namespace mdJucePlugin
{
	SettingsPanelFeel::SettingsPanelFeel(Editor& _editor, Rml::Element* _root) : m_editor(_editor)
	{
		jucePluginEditorLib::SettingsPlugin::createToggleButton(_root, "btPixelPerfectPanel",
			m_editor.getProcessor().getConfig(), PixelPerfectPanel::configKey, [this](bool)
			{
				m_editor.applyPixelPerfectPanel();
			}, PixelPerfectPanel::defaultEnabled);
		jucePluginEditorLib::SettingsPlugin::createToggleButton(_root, "btLcdRotaryInteraction",
			m_editor.getProcessor().getConfig(), lcdInteraction::configKey, [this](bool)
			{
				m_editor.applyLcdInteraction();
			}, lcdInteraction::defaultEnabled);
		{
			auto& config = m_editor.getProcessor().getConfig();
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
		bindGroup(_root, "btWheelSpeed", "panelWheelSpeedPercent");
		bindGroup(_root, "btEncoderSpeed", "panelEncoderSpeedPercent");

		if(auto* const loadFactory = juceRmlUi::helper::findChild(
			_root, "btLoadInstalledFactoryStorage", false))
		{
			juceRmlUi::EventListener::AddClick(loadFactory, [this]
			{
				m_editor.loadInstalledFactoryStorage();
			});
		}
		if(auto* const chooseStorage = juceRmlUi::helper::findChild(
			_root, "btChooseStorageImage", false))
		{
			juceRmlUi::EventListener::AddClick(chooseStorage, [this]
			{
				m_editor.chooseStorageImage();
			});
		}
		m_restoreStorage = juceRmlUi::helper::findChild(
			_root, "btRestorePreviousStorage", false);
		if(m_restoreStorage)
		{
			juceRmlUi::EventListener::AddClick(m_restoreStorage, [this]
			{
				m_editor.restorePreviousStorage();
			});
			updateRestoreAvailability();
			startTimerHz(2);
		}
	}

	void SettingsPanelFeel::timerCallback()
	{
		updateRestoreAvailability();
	}

	void SettingsPanelFeel::updateRestoreAvailability()
	{
		if(m_restoreStorage)
			juceRmlUi::helper::setEnabled(m_restoreStorage,
				m_editor.hasStorageRecoveryImage());
	}

	void SettingsPanelFeel::bindGroup(Rml::Element* _root, const char* _idPrefix, const char* _configKey)
	{
		auto& config = m_editor.getProcessor().getConfig();

		std::vector<Rml::Element*> checkboxes(std::size(Editor::g_panelSpeedPercents), nullptr);

		for (size_t i = 0; i < std::size(Editor::g_panelSpeedPercents); ++i)
		{
			auto* row = juceRmlUi::helper::findChild(_root,
				_idPrefix + std::to_string(Editor::g_panelSpeedPercents[i]), false);
			if (row)
				checkboxes[i] = juceRmlUi::helper::findChild(row, "button");
		}

		const auto updateChecked = [checkboxes, &config, _configKey]
		{
			const auto current = config.getIntValue(_configKey, 100);
			for (size_t i = 0; i < checkboxes.size(); ++i)
			{
				if (checkboxes[i])
					juceRmlUi::ElemButton::setChecked(checkboxes[i], Editor::g_panelSpeedPercents[i] == current);
			}
		};

		updateChecked();

		for (const auto percent : Editor::g_panelSpeedPercents)
		{
			auto* row = juceRmlUi::helper::findChild(_root, _idPrefix + std::to_string(percent), false);
			if (!row)
				continue;

			juceRmlUi::EventListener::AddClick(row, [this, updateChecked, &config, _configKey, percent]
			{
				config.setValue(_configKey, percent);
				config.saveIfNeeded();
				updateChecked();
				m_editor.applyPanelSpeeds();
			});
		}
	}
}
