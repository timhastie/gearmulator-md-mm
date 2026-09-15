#pragma once

#include "jucePluginEditorLib/settingsPlugin.h"

namespace mdJucePlugin
{
	class Editor;

	// RANDOMIZATION settings tab (Machinedrum): chance that a step gets a trig
	// when a random-trig chord fires (single track or every track).
	class SettingsRandomization : public jucePluginEditorLib::SettingsPlugin
	{
	public:
		SettingsRandomization(Editor& _editor, jucePluginEditorLib::Processor& _processor)
			: SettingsPlugin(_processor), m_editor(_editor) {}
		std::string getCategoryName() const override { return "RANDOMIZATION"; }
		std::string getTemplateName() const override { return "tus_settings_randomization_Machinedrum"; }
		void createUi(Rml::Element* _root) override;

	private:
		Editor& m_editor;
	};
}
