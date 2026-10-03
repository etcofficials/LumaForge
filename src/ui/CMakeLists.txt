qt_add_library(lf_ui STATIC
    Theme.h                 Theme.cpp
    Icons.h                 Icons.cpp
    ActionRegistry.h        ActionRegistry.cpp
    Timecode.h              Timecode.cpp
    MainWindow.h            MainWindow.cpp
    AppPaths.h              AppPaths.cpp
    home/HomeScreen.h       home/HomeScreen.cpp
    workspace/WorkspaceManager.h workspace/WorkspaceManager.cpp
    widgets/ToastHost.h     widgets/ToastHost.cpp
    panels/BrowserPanel.h   panels/BrowserPanel.cpp
    panels/ProjectInfoPanel.h panels/ProjectInfoPanel.cpp
    widgets/PanelHeader.h   widgets/PanelHeader.cpp
    widgets/EmptyState.h    widgets/EmptyState.cpp
    widgets/CollapsibleSection.h widgets/CollapsibleSection.cpp
    widgets/ParamSlider.h   widgets/ParamSlider.cpp
    inspector/InspectorSection.h
    inspector/ClipInfoSection.h inspector/ClipInfoSection.cpp
    inspector/TransformSection.h inspector/TransformSection.cpp
    inspector/ColorSection.h inspector/ColorSection.cpp
    inspector/EffectsSection.h inspector/EffectsSection.cpp
    inspector/TextSections.h inspector/TextSections.cpp
    inspector/AudioSection.h inspector/AudioSection.cpp
    inspector/KeyframeControls.h inspector/KeyframeControls.cpp
    inspector/TransitionSection.h inspector/TransitionSection.cpp
    inspector/MaskSection.h inspector/MaskSection.cpp
    inspector/GeneratedSections.h inspector/GeneratedSections.cpp
    widgets/ColorButton.h   widgets/ColorButton.cpp
    panels/MediaPanel.h     panels/MediaPanel.cpp
    panels/EffectsBrowserPanel.h panels/EffectsBrowserPanel.cpp
    panels/TitlesBrowserPanel.h panels/TitlesBrowserPanel.cpp
    panels/TransitionsBrowserPanel.h panels/TransitionsBrowserPanel.cpp
    panels/MarkersPanel.h panels/MarkersPanel.cpp
    panels/MixerPanel.h panels/MixerPanel.cpp
    panels/ScopesPanel.h panels/ScopesPanel.cpp
    export/ExportController.h export/ExportController.cpp
    export/ExportSettingsPanel.h export/ExportSettingsPanel.cpp
    export/ExportQueuePanel.h export/ExportQueuePanel.cpp
    DragMime.h
    panels/InspectorPanel.h panels/InspectorPanel.cpp
    preview/PreviewWidget.h preview/PreviewWidget.cpp
    preview/PreviewPanel.h  preview/PreviewPanel.cpp
    timeline/TimelineView.h timeline/TimelineView.cpp
    timeline/TimelinePanel.h timeline/TimelinePanel.cpp
    dialogs/DiagnosticsDialog.h dialogs/DiagnosticsDialog.cpp
    dialogs/CommandPalette.h dialogs/CommandPalette.cpp
    dialogs/ShortcutEditor.h dialogs/ShortcutEditor.cpp
    dialogs/VoiceOverDialog.h dialogs/VoiceOverDialog.cpp
    dialogs/MediaPropertiesDialog.h dialogs/MediaPropertiesDialog.cpp
    dialogs/ProjectSettingsDialog.h dialogs/ProjectSettingsDialog.cpp
    project/ProjectController.h project/ProjectController.cpp
    media/MediaModel.h      media/MediaModel.cpp
    media/MediaTileDelegate.h media/MediaTileDelegate.cpp
    media/ThumbnailService.h media/ThumbnailService.cpp
    media/ImportController.h media/ImportController.cpp
    media/WaveformService.h media/WaveformService.cpp
    playback/PlaybackController.h playback/PlaybackController.cpp
)
add_library(lumaforge::ui ALIAS lf_ui)
target_include_directories(lf_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(lf_ui
    PUBLIC Qt6::Widgets Qt6::Svg
           lumaforge::core lumaforge::commands lumaforge::hardware lumaforge::rendering lumaforge::diagnostics
           lumaforge::media lumaforge::project lumaforge::playback lumaforge::export)

# Theme + icons are compiled into the binary; no loose files to lose at runtime.
qt_add_resources(lf_ui "lf_ui_resources"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/theme/lumaforge.qss"
        "${CMAKE_SOURCE_DIR}/resources/icons/app.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/new.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/open.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/save.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/import.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/undo.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/redo.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/export.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/play.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/pause.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/stop.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/step-back.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/step-forward.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/go-start.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/go-end.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/split.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/delete.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/zoom-in.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/zoom-out.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/zoom-fit.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/magnet.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/diagnostics.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/media-empty.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/inspector-empty.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/media-video.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/media-audio.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/media-image.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/view-grid.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/view-list.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/sort.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/add-to-timeline.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/audio.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/audio-muted.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/tool-select.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/tool-blade.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/text.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/tool-trim.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/tool-hand.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/tool-zoom.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/loop.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/edit-prev.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/edit-next.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/fullscreen.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/cut.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/copy.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/paste.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/mark-in.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/mark-out.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/save-as.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/snapshot.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/keyboard.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/settings.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/marker.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/folder.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/filter.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/effects.svg"
        "${CMAKE_SOURCE_DIR}/resources/icons/transition.svg"
)
