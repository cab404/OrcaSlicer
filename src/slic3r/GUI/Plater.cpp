#include "Plater.hpp"

#include <iostream>   // XXX
#include <cstddef>
#include <algorithm>
#include <vector>
#include <string>
#include <regex>
#include <boost/filesystem/path.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/notebook.h>
#include <wx/button.h>
#include <wx/bmpcbox.h>
#include <wx/statbox.h>
#include <wx/statbmp.h>
#include <wx/filedlg.h>
#include <wx/dnd.h>
#include <wx/progdlg.h>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/PreviewData.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Polygon.hpp"
// #include "libslic3r/BoundingBox.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "3DScene.hpp"
#include "GLToolbar.hpp"
#include "GUI_Preview.hpp"
#include "Tab.hpp"
#include "PresetBundle.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "ProgressStatusBar.hpp"
#include "slic3r/Utils/ASCIIFolding.hpp"

#include <wx/glcanvas.h>    // Needs to be last because reasons :-/

namespace fs = boost::filesystem;
using Slic3r::_3DScene;
using Slic3r::Preset;


namespace Slic3r {
namespace GUI {


wxDEFINE_EVENT(EVT_SLICING_COMPLETED, wxCommandEvent);
wxDEFINE_EVENT(EVT_PROCESS_COMPLETED, wxCommandEvent);


// Sidebar widgets

// struct InfoBox : public wxStaticBox
// {
//     InfoBox(wxWindow *parent, const wxString &label) :
//         wxStaticBox(parent, wxID_ANY, label)
//     {
//         SetFont(GUI::small_font().Bold());
//     }
// };

class ObjectInfo : public wxStaticBoxSizer
{
public:
    ObjectInfo(wxWindow *parent);

private:
    wxStaticText *info_size;
    wxStaticText *info_volume;
    wxStaticText *info_facets;
    wxStaticText *info_materials;
    wxStaticText *info_manifold;
    wxStaticBitmap *manifold_warning_icon;
};

ObjectInfo::ObjectInfo(wxWindow *parent) :
    wxStaticBoxSizer(new wxStaticBox(parent, wxID_ANY, _(L("Info"))), wxVERTICAL)
{
    // GetStaticBox()->SetFont(GUI::bold_font());   // XXX: ?

    auto *grid_sizer = new wxFlexGridSizer(4, 5, 5);
    grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    grid_sizer->AddGrowableCol(1, 1);
    grid_sizer->AddGrowableCol(3, 1);

    auto init_info_label = [parent, grid_sizer](wxStaticText **info_label, wxString text_label) {
        auto *text = new wxStaticText(parent, wxID_ANY, text_label);
        text->SetFont(GUI::small_font());
        *info_label = new wxStaticText(parent, wxID_ANY, "");
        (*info_label)->SetFont(GUI::small_font());
        grid_sizer->Add(text, 0);
        grid_sizer->Add(*info_label, 0);
    };

    init_info_label(&info_size, _(L("Size")));
    init_info_label(&info_volume, _(L("Volume")));
    init_info_label(&info_facets, _(L("Facets")));
    init_info_label(&info_materials, _(L("Materials")));

    auto *info_manifold_text = new wxStaticText(parent, wxID_ANY, _(L("Manifold")));
    info_manifold_text->SetFont(GUI::small_font());
    info_manifold = new wxStaticText(parent, wxID_ANY, "");
    info_manifold->SetFont(GUI::small_font());
    wxBitmap bitmap(GUI::from_u8(Slic3r::var("error.png")), wxBITMAP_TYPE_PNG);
    manifold_warning_icon = new wxStaticBitmap(parent, wxID_ANY, bitmap);
    auto *sizer_manifold = new wxBoxSizer(wxHORIZONTAL);
    sizer_manifold->Add(info_manifold_text, 0);
    sizer_manifold->Add(manifold_warning_icon, 0, wxLEFT, 2);
    sizer_manifold->Add(info_manifold, 0, wxLEFT, 2);
    grid_sizer->Add(sizer_manifold, 0, wxEXPAND | wxTOP, 4);

    Add(grid_sizer, 0, wxEXPAND);
}

class SlicedInfo : public wxStaticBoxSizer
{
public:
    SlicedInfo(wxWindow *parent);

private:
    wxStaticText *info_filament_m;
    wxStaticText *info_filament_mm3;
    wxStaticText *info_filament_g;
    wxStaticText *info_cost;
    wxStaticText *info_time_normal;
    wxStaticText *info_time_silent;
};

SlicedInfo::SlicedInfo(wxWindow *parent) :
    wxStaticBoxSizer(new wxStaticBox(parent, wxID_ANY, _(L("Sliced Info"))), wxVERTICAL)
{
    // GetStaticBox()->SetFont(GUI::bold_font());   // XXX: ?

    auto *grid_sizer = new wxFlexGridSizer(2, 5, 5);
    grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    grid_sizer->AddGrowableCol(1, 1);
    grid_sizer->AddGrowableCol(3, 1);

    auto init_info_label = [parent, grid_sizer](wxStaticText *&info_label, wxString text_label) {
        auto *text = new wxStaticText(parent, wxID_ANY, text_label);
        text->SetFont(GUI::small_font());
        info_label = new wxStaticText(parent, wxID_ANY, "N/A");
        info_label->SetFont(GUI::small_font());
        grid_sizer->Add(text, 0);
        grid_sizer->Add(info_label, 0);
    };

    init_info_label(info_filament_m, _(L("Used Filament (m)")));
    init_info_label(info_filament_mm3, _(L("Used Filament (mm³)")));
    init_info_label(info_filament_g, _(L("Used Filament (g)")));
    init_info_label(info_cost, _(L("Cost")));
    init_info_label(info_time_normal, _(L("Estimated printing time (normal mode)")));
    init_info_label(info_time_silent, _(L("Estimated printing time (silent mode)")));

    Add(grid_sizer, 0, wxEXPAND);
}


class PresetComboBox : public wxBitmapComboBox
{
public:
    PresetComboBox(wxWindow *parent, Preset::Type preset_type);
    ~PresetComboBox();

private:
    typedef std::size_t Marker;
    enum { LABEL_ITEM_MARKER = 0x4d };

    Preset::Type preset_type;
    int last_selected;
};

PresetComboBox::PresetComboBox(wxWindow *parent, Preset::Type preset_type) :
    wxBitmapComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY),
    preset_type(preset_type),
    last_selected(wxNOT_FOUND)
{
    Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &evt) {
        auto selected_item = this->GetSelection();

        auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
        if (marker == LABEL_ITEM_MARKER) {
            this->SetSelection(this->last_selected);
            evt.StopPropagation();
        } else if (this->last_selected != selected_item) {
            this->last_selected = selected_item;
            evt.SetInt(this->preset_type);
        } else {
            evt.StopPropagation();
        }
    });
}

PresetComboBox::~PresetComboBox() {}


// Sidebar / private

struct Sidebar::priv
{
    // Sidebar *q;      // PIMPL back pointer ("Q-Pointer")

    wxScrolledWindow *scrolled;

    wxFlexGridSizer *sizer_presets;
    PresetComboBox *combo_print;
    std::vector<PresetComboBox*> combos_filament;
    wxBoxSizer *sizer_filaments;
    PresetComboBox *combo_sla_material;
    PresetComboBox *combo_printer;

    wxBoxSizer *sizer_params;
    ObjectInfo *object_info;
    SlicedInfo *sliced_info;

    wxButton *btn_export_gcode;
    wxButton *btn_reslice;
    // wxButton *btn_print;  // XXX: remove
    wxButton *btn_send_gcode;
};


// Sidebar / public

Sidebar::Sidebar(wxWindow *parent)
    : wxPanel(parent), p(new priv)
{
    p->scrolled = new wxScrolledWindow(this);
    p->scrolled->SetScrollbars(0, 1, 1, 1);   // XXX

    // The preset chooser
    p->sizer_presets = new wxFlexGridSizer(4, 2, 1, 2);
    p->sizer_presets->AddGrowableCol(1, 1);
    p->sizer_presets->SetFlexibleDirection(wxHORIZONTAL);
    p->sizer_filaments = new wxBoxSizer(wxVERTICAL);

    auto init_combo = [this](PresetComboBox **combo, wxString label, Preset::Type preset_type, bool filament) {
        auto *text = new wxStaticText(this, wxID_ANY, label);
        text->SetFont(GUI::small_font());
        // combo = new wxBitmapComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
        *combo = new PresetComboBox(this, preset_type);

        auto *sizer_presets = this->p->sizer_presets;
        auto *sizer_filaments = this->p->sizer_filaments;
        sizer_presets->Add(text, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        if (! filament) {
            sizer_presets->Add(*combo, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxBOTTOM, 1);
        } else {
            sizer_filaments->Add(*combo, 1, wxEXPAND | wxBOTTOM, 1);
            sizer_presets->Add(sizer_filaments, 1, wxEXPAND);
        }
    };

    p->combos_filament.push_back(nullptr);
    init_combo(&p->combo_print, _(L("Print settings")), Preset::TYPE_PRINT, false);
    init_combo(&p->combos_filament[0], _(L("Filament")), Preset::TYPE_FILAMENT, true);
    init_combo(&p->combo_sla_material, _(L("SLA material")), Preset::TYPE_SLA_MATERIAL, false);
    init_combo(&p->combo_printer, _(L("Printer")), Preset::TYPE_PRINTER, false);

    // Frequently changed parameters
    p->sizer_params = new wxBoxSizer(wxVERTICAL);
    // GUI::add_frequently_changed_parameters(this, p->sizer_params, p->sizer_presets);

    // Buttons in the scrolled area
    wxBitmap arrow_up(GUI::from_u8(Slic3r::var("brick_go.png")), wxBITMAP_TYPE_PNG);
    p->btn_send_gcode = new wxButton(this, wxID_ANY, _(L("Send to printer")));
    p->btn_send_gcode->SetBitmap(arrow_up);
    p->btn_send_gcode->Hide();
    auto *btns_sizer_scrolled = new wxBoxSizer(wxHORIZONTAL);
    std::cerr << "btns_sizer_scrolled: " << btns_sizer_scrolled << std::endl;
    btns_sizer_scrolled->Add(p->btn_send_gcode);

    // Info boxes
    p->object_info = new ObjectInfo(this);
    p->sliced_info = new SlicedInfo(this);

    // Sizer in the scrolled area
    auto *scrolled_sizer = new wxBoxSizer(wxVERTICAL);
    std::cerr << "scrolled_sizer: " << scrolled_sizer << std::endl;
    scrolled_sizer->Add(p->sizer_presets, 0, wxEXPAND | wxLEFT, 2);
    scrolled_sizer->Add(p->object_info, 1, wxEXPAND);
    scrolled_sizer->Add(btns_sizer_scrolled, 0, wxEXPAND, 0);
    scrolled_sizer->Add(p->sliced_info, 0, wxEXPAND);

    // Buttons underneath the scrolled area
    p->btn_export_gcode = new wxButton(this, wxID_ANY, _(L("Export G-code…")));
    p->btn_reslice = new wxButton(this, wxID_ANY, _(L("Slice now")));

    auto *btns_sizer = new wxBoxSizer(wxVERTICAL);
    std::cerr << "btns_sizer: " << btns_sizer << std::endl;
    btns_sizer->Add(p->btn_reslice);
    btns_sizer->Add(p->btn_export_gcode);

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    std::cerr << "sizer: " << sizer << std::endl;
    sizer->Add(scrolled_sizer);
    sizer->Add(btns_sizer);
    SetSizer(sizer);
    SetMinSize(wxSize(320, -1));
}

Sidebar::~Sidebar() {}

void Sidebar::update_presets(Preset::Type preset_type)
{
    // TODO: wxApp access

    switch (preset_type) {
    case Preset::TYPE_FILAMENT:
        // my $choice_idx = 0;
        if (p->combos_filament.size() == 1) {
            // Single filament printer, synchronize the filament presets.
            // wxTheApp->{preset_bundle}->set_filament_preset(0, wxTheApp->{preset_bundle}->filament->get_selected_preset->name);
        }

        for (size_t i = 0; i < p->combos_filament.size(); i++) {
            // wxTheApp->{preset_bundle}->update_platter_filament_ui($choice_idx, $choice);
        }
        break;

    case Preset::TYPE_PRINT:
        // wxTheApp->{preset_bundle}->print->update_platter_ui($choosers[0]);
        break;

    case Preset::TYPE_SLA_MATERIAL:
        // wxTheApp->{preset_bundle}->sla_material->update_platter_ui($choosers[0]);
        break;

    case Preset::TYPE_PRINTER:
        // Update the print choosers to only contain the compatible presets, update the dirty flags.
        // wxTheApp->{preset_bundle}->print->update_platter_ui($self->{preset_choosers}{print}->[0]);
        // Update the printer choosers, update the dirty flags.
        // wxTheApp->{preset_bundle}->printer->update_platter_ui($choosers[0]);
        // Update the filament choosers to only contain the compatible presets, update the color preview,
        // update the dirty flags.
        for (size_t i = 0; i < p->combos_filament.size(); i++) {
            // wxTheApp->{preset_bundle}->update_platter_filament_ui($choice_idx, $choice);
        }
        break;

    default: break;
    }

    // Synchronize config.ini with the current selections.
    // wxTheApp->{preset_bundle}->export_selections(wxTheApp->{app_config});
}


// Plater::Object

struct PlaterObject
{
    std::string name;
    bool selected;

    PlaterObject(std::string name) : name(std::move(name)), selected(false) {}
};

// Plater::DropTarget

class PlaterDropTarget : public wxFileDropTarget
{
public:
    PlaterDropTarget(Plater *plater) : plater(plater) {}

    virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames);

private:
    Plater *plater;
};

bool PlaterDropTarget::OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames)
{
    // TODO
    // return false;
    throw 0;
}

// Plater / private

struct Plater::priv
{
    // PIMPL back pointer ("Q-Pointer")
    Plater *q;
    MainFrame *main_frame;

    // Data
    Slic3r::DynamicPrintConfig *config;
    Slic3r::Print print;
    Slic3r::Model model;
    Slic3r::GCodePreviewData gcode_preview_data;
    std::vector<PlaterObject> objects;

    std::string export_gcode_output_file;
    std::string send_gcode_file;

    // GUI elements
    wxNotebook *notebook;
    Sidebar *sidebar;
    wxGLCanvas *canvas3D;    // TODO: Use GLCanvas3D when we can
    GUI::Preview *preview;
    BackgroundSlicingProcess background_process;

    static const int gl_attrs[];
    static const std::regex pattern_bundle;
    static const std::regex pattern_3mf;
    static const std::regex pattern_zip_amf;

    priv(Plater *q, MainFrame *main_frame);

    std::vector<int> collect_selections();
    void update(bool force_autocenter = false);
    void update_ui_from_settings();
    ProgressStatusBar* statusbar();
    std::vector<size_t> load_files(const std::vector<fs::path> &input_files);
    size_t load_model_objects(const ModelObjectPtrs &model_objects);

    void on_notebook_changed(wxBookCtrlEvent &);
    void on_select_preset(wxCommandEvent &);
    void on_update_print_preview(wxCommandEvent &);
    void on_process_completed(wxCommandEvent &);
    void on_layer_editing_toggled(bool enable);
    void on_action_add(const wxCommandEvent&);
};

// TODO: multisample, see 3DScene.pm
const int Plater::priv::gl_attrs[] = {WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 24, 0};
const std::regex Plater::priv::pattern_bundle("[.](amf|amf[.]xml|zip[.]amf|3mf|prusa)$", std::regex::icase);
const std::regex Plater::priv::pattern_3mf("[.]3mf$", std::regex::icase);
const std::regex Plater::priv::pattern_zip_amf("[.]zip[.]amf$", std::regex::icase);

Plater::priv::priv(Plater *q, MainFrame *main_frame) :
    q(q),
    main_frame(main_frame),
    config(Slic3r::DynamicPrintConfig::new_from_defaults_keys({
        "bed_shape", "complete_objects", "extruder_clearance_radius", "skirts", "skirt_distance",
        "brim_width", "variable_layer_height", "serial_port", "serial_speed", "host_type", "print_host",
        "printhost_apikey", "printhost_cafile", "nozzle_diameter", "single_extruder_multi_material",
        "wipe_tower", "wipe_tower_x", "wipe_tower_y", "wipe_tower_width", "wipe_tower_rotation_angle",
        "extruder_colour", "filament_colour", "max_print_height", "printer_model"
    })),
    notebook(new wxNotebook(q, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_BOTTOM)),
    sidebar(new Sidebar(q)),
    canvas3D(new wxGLCanvas(notebook, wxID_ANY, gl_attrs))
{
    background_process.set_print(&print);
    background_process.set_gcode_preview_data(&gcode_preview_data);
    background_process.set_sliced_event(EVT_SLICING_COMPLETED);
    background_process.set_finished_event(EVT_PROCESS_COMPLETED);

    _3DScene::add_canvas(canvas3D);
    notebook->AddPage(canvas3D, _(L("3D")));
    preview = new GUI::Preview(notebook, config, &print, &gcode_preview_data);

    // XXX: If have OpenGL
    _3DScene::enable_picking(canvas3D, true);
    _3DScene::enable_moving(canvas3D, true);
    // XXX: more config from 3D.pm
    // Slic3r::GUI::_3DScene::set_select_by($self, 'object');
    // Slic3r::GUI::_3DScene::set_drag_by($self, 'instance');
    // Slic3r::GUI::_3DScene::set_model($self, $model);
    // Slic3r::GUI::_3DScene::set_print($self, $print);
    // Slic3r::GUI::_3DScene::set_config($self, $config);
    _3DScene::enable_gizmos(canvas3D, true);
    _3DScene::enable_toolbar(canvas3D, true);
    _3DScene::enable_shader(canvas3D, true);
    _3DScene::enable_force_zoom_to_bed(canvas3D, true);

    // XXX: apply_config_timer
    // {
    //  my $timer_id = Wx::NewId();
    //  $self->{apply_config_timer} = Wx::Timer->new($self, $timer_id);
    //  EVT_TIMER($self, $timer_id, sub {
    //      my ($self, $event) = @_;
    //      $self->async_apply_config;
    //  });
    // }

    auto *bed_shape = config->opt<ConfigOptionPoints>("bed_shape");
    _3DScene::set_bed_shape(canvas3D, bed_shape->values);
    _3DScene::zoom_to_bed(canvas3D);
    preview->set_bed_shape(bed_shape->values);

    update();

    // TODO: Preview::register_on_viewport_changed_callback is Perl-only

    auto *hsizer = new wxBoxSizer(wxHORIZONTAL);
    hsizer->Add(notebook, 1, wxEXPAND | wxTOP, 1);
    hsizer->Add(sidebar, 0, wxEXPAND | wxLEFT | wxRIGHT, 0);
    q->SetSizer(hsizer);

    // Events:

    // Notebook page change event
    notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &priv::on_notebook_changed, this);

    // Preset change event
    sidebar->Bind(wxEVT_COMBOBOX, &priv::on_select_preset, this);

    // Sidebar button events
    sidebar->p->btn_export_gcode->Bind(wxEVT_BUTTON, [q](wxCommandEvent&) { q->export_gcode(""); });
    sidebar->p->btn_reslice->Bind(wxEVT_BUTTON, [q](wxCommandEvent&) { q->reslice(); });
    sidebar->p->btn_send_gcode->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        this->send_gcode_file = this->q->export_gcode("");
    });

    // 3DScene events:
    // TODO: (?)
    // on_layer_editing_toggled
    // on_action_add
    canvas3D->Bind(EVT_GLTOOLBAR_ADD, &priv::on_action_add, this);

    q->Bind(EVT_SLICING_COMPLETED, &priv::on_update_print_preview, this);
    q->Bind(EVT_PROCESS_COMPLETED, &priv::on_process_completed, this);

    // Drop target:
    q->SetDropTarget(new PlaterDropTarget(q));   // if my understanding is right, wxWindow takes the owenership

    // TODO: ?
    //  # Send sizers/buttons to C++
    // Slic3r::GUI::set_objects_from_perl( $self->{scrolled_window_panel},
    //                                     $frequently_changed_parameters_sizer,
    //                                     $info_sizer,
    //                                     $self->{btn_export_gcode},
    //     #                                $self->{btn_export_stl},
    //                                     $self->{btn_reslice},
    //                                     $self->{btn_print},
    //                                     $self->{btn_send_gcode},
    //                                     $self->{object_info_manifold_warning_icon} );

    // Slic3r::GUI::set_model_events_from_perl(  $self->{model},
    //                                     $self->{event_object_selection_changed},
    //                                     $self->{event_object_settings_changed},
    //                                     $self->{event_remove_object},
    //                                     $self->{event_update_scene});

    update_ui_from_settings();
    q->Layout();
}

std::vector<int> Plater::priv::collect_selections()
{
    std::vector<int> res;
    for (const auto &obj : objects) {
        res.push_back(obj.selected);
    }
    return res;
}

void Plater::priv::update(bool force_autocenter)
{
    // TODO
}

void Plater::priv::update_ui_from_settings()
{
    // TODO: (?)
    // my ($self) = @_;
    // if (defined($self->{btn_reslice}) && $self->{buttons_sizer}->IsShown($self->{btn_reslice}) != (! wxTheApp->{app_config}->get("background_processing"))) {
    //     $self->{buttons_sizer}->Show($self->{btn_reslice}, ! wxTheApp->{app_config}->get("background_processing"));
    //     $self->{buttons_sizer}->Layout;
    // }
}

ProgressStatusBar*  Plater::priv::statusbar()
{
    return main_frame->m_statusbar;
}

std::vector<size_t> Plater::priv::load_files(const std::vector<fs::path> &input_files)
{
    if (input_files.size() < 1) { return std::vector<size_t>(); }

    auto *nozzle_dmrs = config->opt<ConfigOptionFloats>("nozzle_diameter");

    bool one_by_one = input_files.size() == 1 || nozzle_dmrs->values.size() <= 1;
    if (! one_by_one) {
        for (const auto &path : input_files) {
            if (std::regex_match(path.string(), pattern_bundle)) {
                one_by_one = true;
                break;
            }
        }
    }

    const auto loading = _(L("Loading…"));
    wxProgressDialog dlg(loading, loading);
    dlg.Pulse();

    auto *new_model = one_by_one ? nullptr : new Slic3r::Model();
    std::vector<size_t> obj_idx;   // XXX: ?

    for (size_t i = 0; i < input_files.size(); i++) {
        const auto &path = input_files[i];
        const auto filename = path.filename();
        const auto dlg_info = wxString::Format(_(L("Processing input file %s\n")), filename.string());
        dlg.Update(100 * i / input_files.size(), dlg_info);

        const bool type_3mf = std::regex_match(path.string(), pattern_3mf);
        const bool type_zip_amf = !type_3mf && std::regex_match(path.string(), pattern_zip_amf);

        Slic3r::Model model;
        try {
            if (type_3mf || type_zip_amf) {
                DynamicPrintConfig config;
                config.apply(FullPrintConfig::defaults());
                model = Slic3r::Model::read_from_archive(path.string(), &config, false);
                Preset::normalize(config);
                wxGetApp().preset_bundle->load_config_model(filename.string(), std::move(config));
                for (const auto &kv : main_frame->options_tabs()) { kv.second->load_current_preset(); }
                wxGetApp().app_config->update_config_dir(path.parent_path().string());
                // forces the update of the config here, or it will invalidate the imported layer heights profile if done using the timer
                // and if the config contains a "layer_height" different from the current defined one
                // TODO:
                // $self->async_apply_config;
            } else {
                model = Slic3r::Model::read_from_file(path.string(), nullptr, false);
            }
        }
        catch (const std::runtime_error &e) {
            GUI::show_error(q, e.what());
            continue;
        }

        // The model should now be initialized

        if (model.looks_like_multipart_object()) {
            wxMessageDialog dlg(q, _(L(
                    "This file contains several objects positioned at multiple heights. "
                    "Instead of considering them as multiple objects, should I consider\n"
                    "this file as a single object having multiple parts?\n"
                )), _(L("Multi-part object detected")), wxICON_WARNING | wxYES | wxNO);
            if (dlg.ShowModal() == wxID_YES) {
                model.convert_multipart_object(nozzle_dmrs->values.size());
            }
        }

        if (type_3mf) {
            for (ModelObject* model_object : model.objects) {
                model_object->center_around_origin();
            }
        }

        if (one_by_one) {
            // TODO:
            // push @obj_idx, $self->load_model_objects(@{$model->objects});
            // obj_idx.push_back(load_model_objects(model.objects);
        } else {
            // This must be an .stl or .obj file, which may contain a maximum of one volume.
            for (const ModelObject* model_object : model.objects) {
                new_model->add_object(*model_object);
            }
        }
    }

    if (new_model != nullptr) {
        wxMessageDialog dlg(q, _(L(
                "Multiple objects were loaded for a multi-material printer.\n"
                "Instead of considering them as multiple objects, should I consider\n"
                "these files to represent a single object having multiple parts?\n"
            )), _(L("Multi-part object detected")), wxICON_WARNING | wxYES | wxNO);
        if (dlg.ShowModal() == wxID_YES) {
            new_model->convert_multipart_object(nozzle_dmrs->values.size());
        }

        // TODO:
        // push @obj_idx, $self->load_model_objects(@{$new_model->objects});
        // obj_idx.push_back(load_model_objects(new_model->objects);
    }

    wxGetApp().app_config->update_skein_dir(input_files[input_files.size() - 1].parent_path().string());
    // XXX: Plater.pm had @loaded_files, but didn't seem to fill them with the filenames...
    statusbar()->set_status_text(_(L("Loaded")));
    return obj_idx;
}


// TODO: move to Point.hpp
// inline Vec3crd to_3d(const Vec2crd &pt2, coord_t z) { return Vec3crd(pt2(0), pt2(1), z); }
// inline Vec3i64 to_3d(const Vec2i64 &pt2, int64_t z) { return Vec3i64(pt2(0), pt2(1), z); }
// inline Vec3f   to_3d(const Vec2f   &pt2, float z)   { return Vec3f  (pt2(0), pt2(1), z); }
// inline Vec3d   to_3d(const Vec2d   &pt2, double z)  { return Vec3d  (pt2(0), pt2(1), z); }
// typedef Eigen::Matrix<coord_t,  2, 1, Eigen::DontAlign> Vec2crd;
template<class T> Eigen::Matrix<T, 3, 1, Eigen::DontAlign> to_3d(const Eigen::Matrix<T, 2, 1, Eigen::DontAlign> &m, T z)
{
    return Eigen::Matrix<T, 3, 1, Eigen::DontAlign>(m(0), m(1), z);
}

Vec3crd to_3d(const Point &p, coord_t z)
{
    return Vec3crd(p(0), p(1), z);
}

size_t Plater::priv::load_model_objects(const ModelObjectPtrs &model_objects)
{
    // TODO

    auto *bed_shape_opt = config->opt<ConfigOptionPoints>("bed_shape");
    const auto bed_shape = Slic3r::Polygon::new_scale(bed_shape_opt->values);
    const BoundingBox bed_shape_bb = bed_shape.bounding_box();
    // const Point bed_center_2 = bed_shape_bb.center();
    // const Vec3d bed_center(bed_center_2(0), bed_center_2(1), 0.0);
    // const Vec3d bed_center = to_3d(bed_shape_bb.center(), 0).cast<double>();
    const Vec3d bed_center = to_3d<double>(bed_shape_bb.center().cast<double>(), 0.0);
    const Vec2d bed_size = bed_shape_bb.size().cast<double>();

    bool need_arrange = false;
    bool scaled_down = false;
    // my @obj_idx = ();   // XXX
    std::vector<size_t> obj_idx;

    for (const ModelObject *model_object : model_objects) {
        auto *object = model.add_object(*model_object);
        std::string object_name = object->name.empty() ? fs::path(object->input_file).filename().string() : object->name;
        objects.emplace_back(std::move(object_name));
        obj_idx.push_back(objects.size() - 1);

        if (model_object->instances.size() == 0) {
            // if object has no defined position(s) we need to rearrange everything after loading
            need_arrange = true;

            // add a default instance and center object around origin
            object->center_around_origin();  // also aligns object to Z = 0
            auto *instance = object->add_instance();
            instance->set_offset(bed_center);
        }

        // my $size = $o->bounding_box->size;
        // const auto size = object->bounding_box().size();
        // my $ratio = max($size->x / unscale($bed_size->x), $size->y / unscale($bed_size->y));
        // size.cwiseQuotient(bed_size);
        // const auto = size / bed_size;
        // const auto ratio = std::max(size.x / Slic3r::unscale(bed_size(0)), size.y / Slic3r::unscale(bed_size(1)));
        // if ($ratio > 10000) {
        //     # the size of the object is too big -> this could lead to overflow when moving to clipper coordinates,
        //     # so scale down the mesh
        //     $o->scale_xyz(Slic3r::Pointf3->new(1/$ratio, 1/$ratio, 1/$ratio));
        //     $scaled_down = 1;
        // }
        // elsif ($ratio > 5) {
        //     $_->set_scaling_factor(1/$ratio) for @{$o->instances};
        //     $scaled_down = 1;
        // }
    }

    throw 0;
    return 0;
}

void Plater::priv::on_notebook_changed(wxBookCtrlEvent&)
{
    const auto current_id = notebook->GetCurrentPage()->GetId();
    if (current_id == canvas3D->GetId()) {
        if (_3DScene::is_reload_delayed(canvas3D)) {
            _3DScene::set_objects_selections(canvas3D, collect_selections());
            _3DScene::reload_scene(canvas3D, true);
        }
        // sets the canvas as dirty to force a render at the 1st idle event (wxWidgets IsShownOnScreen() is buggy and cannot be used reliably)
        _3DScene::set_as_dirty(canvas3D);
    } else if (current_id == preview->GetId()) {
        preview->reload_print();
        preview->set_canvas_as_dirty();
    }
}

void Plater::priv::on_select_preset(wxCommandEvent &evt)
{
    auto preset_type = static_cast<Preset::Type>(evt.GetInt());
    auto *combo = static_cast<wxBitmapComboBox*>(evt.GetEventObject());

    if (preset_type == Preset::TYPE_FILAMENT) {
        // FIXME:
        // wxTheApp->{preset_bundle}->set_filament_preset($idx, $choice->GetStringSelection);
    }

    // TODO: ?
    if (false) {
    // if ($group eq 'filament' && @{$self->{preset_choosers}{filament}} > 1) {
    //  # Only update the platter UI for the 2nd and other filaments.
    //  wxTheApp->{preset_bundle}->update_platter_filament_ui($idx, $choice);
    // }
    } else {
        auto selected_item = combo->GetSelection();

        // TODO: Handle by an event handler in MainFrame, if needed
    }

    // TODO:
    // # Synchronize config.ini with the current selections.
    // wxTheApp->{preset_bundle}->export_selections(wxTheApp->{app_config});
    // # get new config and generate on_config_change() event for updating plater and other things
    // $self->on_config_change(wxTheApp->{preset_bundle}->full_config);
}

void Plater::priv::on_update_print_preview(wxCommandEvent &)
{
    // TODO
}

void Plater::priv::on_process_completed(wxCommandEvent &)
{
    // TODO
}

void Plater::priv::on_layer_editing_toggled(bool enable)
{
    _3DScene::enable_layers_editing(canvas3D, enable);
    if (enable && !_3DScene::is_layers_editing_enabled(canvas3D)) {
        // Initialization of the OpenGL shaders failed. Disable the tool.
        _3DScene::enable_toolbar_item(canvas3D, "layersediting", false);
    }
    canvas3D->Refresh();
    canvas3D->Update();
}

void Plater::priv::on_action_add(const wxCommandEvent&)
{
    wxArrayString input_files;
    wxGetApp().open_model(q, input_files);

    std::vector<fs::path> input_paths;
    for (const auto &file : input_files) {
        input_paths.push_back(file.wx_str());
    }
    load_files(input_paths);
}

// Plater / Public

Plater::Plater(wxWindow *parent, MainFrame *main_frame)
    : wxPanel(parent), p(new priv(this, main_frame))
{
    // Initialization performed in the private c-tor
}

Plater::~Plater()
{
    _3DScene::remove_canvas(p->canvas3D);
}

Sidebar& Plater::sidebar() { return *p->sidebar; }

std::string Plater::export_gcode(const std::string &output_path)
{
    if (p->objects.size() == 0) { return ""; }

    if (! p->export_gcode_output_file.empty()) {
        GUI::show_error(this, _(L("Another export job is currently running.")));
        return "";
    }

    // wxTheApp->{preset_bundle}->full_config->validate;   // FIXME
    const std::string err = p->print.validate();
    if (! err.empty()) {
        // The config is not valid
        GUI::show_error(this, _(err));
        return "";
    }

    // Copy the names of active presets into the placeholder parser.
    // wxTheApp->{preset_bundle}->export_selections_pp($self->{print}->placeholder_parser);   // FIXME

    // select output file
    if (! output_path.empty()) {
        p->export_gcode_output_file = p->print.output_filepath(output_path);
        // FIXME: ^ errors to handle?
    } else {
        // FIXME:
        std::string default_output_file;  // FIXME: tmp
        // my $default_output_file = eval { $self->{print}->output_filepath($main::opt{output} // '') };
        // Slic3r::GUI::catch_error($self) and return;

        // If possible, remove accents from accented latin characters.
        // This function is useful for generating file names to be processed by legacy firmwares.
        default_output_file = Slic3r::fold_utf8_to_ascii(default_output_file);
        wxFileDialog dlg(this, _(L("Save G-code file as:")),
            wxEmptyString,
            wxEmptyString,
            Slic3r::GUI::FILE_WILDCARDS.at("gcode"),
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT
        );
        // FIXME: ^ defaultDir:
        // wxTheApp->{app_config}->get_last_output_dir(dirname($default_output_file)),
        // FIXME: ^ defaultFile:
        // basename($default_output_file), &Slic3r::GUI::FILE_WILDCARDS->{gcode}, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

        if (dlg.ShowModal() != wxID_OK) { return ""; }
        auto path = dlg.GetPath();
        // wxTheApp->{app_config}->update_last_output_dir(dirname($path));   // FIXME
        p->export_gcode_output_file = path;
    }

    return p->export_gcode_output_file;
}

void Plater::reslice()
{
    // TODO
}


}}    // namespace Slic3r::GUI
