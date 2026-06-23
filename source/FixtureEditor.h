#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FixtureModel.h"
#include "ChannelRow.h"
#include <vector>
#include <memory>

/**
    Editor completo de equipos (fixtures), presentado como overlay.

    Incluye datos del equipo (nombre/fabricante/modelo), plantillas rapidas por
    tipo de luz, una tabla editable de canales (tipo, nombre, valor por defecto),
    y el "patch" (universo, direccion DMX y cantidad para crear varias copias).
*/
class FixtureEditor : public juce::Component
{
public:
    FixtureEditor();
    ~FixtureEditor() override;

    /** Carga un equipo existente para editarlo (oculta la cantidad). */
    void loadFixture (const Fixture& f);

    /** true si esta en modo edicion (un solo equipo) en vez de alta. */
    bool isEditing = false;

    std::function<void (std::vector<Fixture>)> onCommit;   // equipos resultantes
    std::function<void()>                      onCancel;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ChannelList; // contenedor interno con las filas

    void applyTemplate (int templateId);
    void addChannel (const ChannelDef& def);
    void rebuildIndices();
    void updateFootprint();
    std::vector<Fixture> buildFixtures() const;

    juce::Label    titleLabel;

    juce::Label    nameLabel, manuLabel, modelLabel;
    juce::TextEditor nameEditor, manuEditor, modelEditor;

    juce::Label    templateLabel;
    juce::ComboBox templateCombo;

    juce::Label    channelsHeader;
    juce::TextButton addChannelButton { "+ Canal" };
    juce::Viewport viewport;
    std::unique_ptr<ChannelList> channelList;

    juce::Label  universeLabel, addressLabel, quantityLabel;
    juce::Slider universeSlider, addressSlider, quantitySlider;

    juce::Label    footprintLabel;
    juce::TextButton saveButton   { "Guardar" };
    juce::TextButton cancelButton { "Cancelar" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FixtureEditor)
};
