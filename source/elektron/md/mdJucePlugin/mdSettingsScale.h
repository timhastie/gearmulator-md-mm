#pragma once

#include "jucePluginEditorLib/settingsPlugin.h"

namespace mdJucePlugin
{
	class Editor;

	// SCALE settings tab: scale quantizer scale and root (Machinedrum).
	class SettingsScale : public jucePluginEditorLib::SettingsPlugin
	{
	public:
		SettingsScale(Editor& _editor, jucePluginEditorLib::Processor& _processor)
			: SettingsPlugin(_processor), m_editor(_editor) {}
		std::string getCategoryName() const override { return "SCALE"; }
		std::string getTemplateName() const override;
		void createUi(Rml::Element* _root) override;

	private:
		Editor& m_editor;
	};
}
