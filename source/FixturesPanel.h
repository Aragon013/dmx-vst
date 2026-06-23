#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "FixtureEditor.h"

/** Pestana de equipos: lista de fixtures con alta, edicion y baja manual. */
class FixturesPanel : public juce::Component,
                      private juce::ListBoxModel
{
public:
    explicit FixturesPanel (DmxVstAudioProcessor&);

    void resized() override;
    void refresh();

    // ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

private:
    void showEditorForNew();
    void showEditorForEdit (int index);
    void closeEditor();
    void removeSelected();

    DmxVstAudioProcessor& processorRef;

    juce::ListBox    list { "fixtures", this };
    juce::TextButton addButton    { "Anadir equipo" };
    juce::TextButton editButton   { "Editar" };
    juce::TextButton removeButton { "Quitar" };

    std::unique_ptr<FixtureEditor> editor;
    int editingIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FixturesPanel)
};
