#include "mdSettingsRandomization.h"

#include "mdEditor.h"
#include "mdController.h"
#include "juceRmlUi/rmlElemButton.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/StringUtilities.h"

#include <algorithm>
#include <cmath>

namespace mdJucePlugin
{
	std::string SettingsRandomization::getTemplateName() const
	{
		return "tus_settings_randomization_" + m_editor.getSettingsTemplateSuffix();
	}

	void SettingsRandomization::createUi(Rml::Element* _root)
	{
		// Include/exclude grid: checked = included.
		{
			auto& controller = m_editor.getMdController();
			const struct { const char* prefix; Controller::RandomizeAspect aspect; } columns[] =
			{
				{"exTrig", Controller::RandomizeAspect::Trigs},
				{"exMach", Controller::RandomizeAspect::Machines},
				{"exLock", Controller::RandomizeAspect::Locks},
			};
			for(const auto& column : columns)
			{
				for(uint8_t track = 0; track < 16; ++track)
				{
					auto* const row = juceRmlUi::helper::findChild(_root, column.prefix + std::to_string(track), false);
					if(!row)
						continue;
					auto* const button = juceRmlUi::helper::findChildT<juceRmlUi::ElemButton>(row, "button");
					if(!button)
						continue;
					const auto aspect = column.aspect;
					juceRmlUi::ElemButton::setChecked(button, !controller.isTrackExcluded(aspect, track));
					juceRmlUi::EventListener::AddClick(row, [&controller, button, aspect, track]
					{
						const auto include = !juceRmlUi::ElemButton::isChecked(button);
						controller.setTrackExcluded(aspect, track, !include);
						juceRmlUi::ElemButton::setChecked(button, include);
					});
				}
			}
		}

		auto& controller = m_editor.getMdController();
		const auto bindSlider = [_root, &controller](const char* _sliderId, const char* _labelId,
			int (Controller::*_get)() const, void (Controller::*_set)(int), const int _default)
		{
			auto* const slider = juceRmlUi::helper::findChild(_root, _sliderId, false);
			auto* const label = juceRmlUi::helper::findChild(_root, _labelId, false);
			if(!slider || !label)
				return;
			const auto show = [label](const int _percent)
			{
				label->SetInnerRML(Rml::StringUtilities::EncodeRml(std::to_string(_percent) + " %"));
			};
			const auto current = (controller.*_get)();
			slider->SetAttribute("value", std::to_string(current));
			show(current);
			juceRmlUi::EventListener::Add(slider, Rml::EventId::Change, [slider, &controller, show, _set](Rml::Event& _event)
			{
				_event.StopPropagation();
				const auto* const value = slider->GetAttribute("value");
				if(!value)
					return;
				const auto percent = std::clamp(static_cast<int>(std::lround(
					value->Get<float>(slider->GetCoreInstance()))), 1, 99);
				(controller.*_set)(percent);
				show(percent);
			});
			juceRmlUi::EventListener::Add(slider, Rml::EventId::Dblclick, [slider, &controller, show, _set, _default](Rml::Event& _event)
			{
				_event.StopPropagation();
				slider->SetAttribute("value", std::to_string(_default));
				(controller.*_set)(_default);
				show(_default);
			});
		};
		bindSlider("sliderTrigChance", "labelTrigChance", &Controller::getTrigChancePercent,
			&Controller::setTrigChancePercent, Controller::g_trigChanceDefault);
		bindSlider("sliderLockChance", "labelLockChance", &Controller::getLockChancePercent,
			&Controller::setLockChancePercent, Controller::g_lockChanceDefault);
	}
}
