"""
TomTom Face Studio - Desktop Authoring Application
==================================================
Desktop authoring tool for TomTom ONE (v6) watch faces (320x240 LCD).
Supports hand drawing, artwork import, element layout, live preview,
device state simulation, declarative rules, and .ttface package export.
"""

import sys
import os
import copy
import tempfile
from datetime import datetime
from PIL import Image, ImageDraw

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGridLayout, QLabel, QPushButton, QComboBox, QSpinBox, QSlider,
    QLineEdit, QColorDialog, QFileDialog, QTabWidget, QListWidget,
    QListWidgetItem, QCheckBox, QMessageBox, QGroupBox, QSplitter,
    QFrame, QAction, QToolBar
)
from PyQt5.QtCore import Qt, QTimer, QSize
from PyQt5.QtGui import QImage, QPixmap, QColor, QIcon, QPainter, QPen

# Local module imports
from studio.core.canvas import CanvasModel
from studio.core.elements import render_element
from studio.core.rules import RuleEngine
from studio.core.exporter import export_ttface_package, validate_face_project
from studio.core.importer import save_project_file, load_project_file, inspect_ttface_package
from studio.core.gallery import export_ttgallery
from studio.core.formats import FORMAT_REGISTRY, get_project_formats
from studio.core.device_control import (
    DEVICE_FACE_OPTIONS,
    DeviceControlError,
    read_device_face,
    select_device_face,
)
from studio.core.data_fields import (
    SUPPORTED_DATA_FIELDS,
    validate_data_requirements,
    has_weather_requirements,
    get_provider_capabilities,
)
from studio.core.weather import SharedWeatherService


class CanvasWidget(QLabel):
    """Interactive canvas widget displaying 320x240 face with zoom and grid."""

    def __init__(self, studio_app, parent=None):
        super().__init__(parent)
        self.studio = studio_app
        self.zoom = 2
        self.show_grid = True
        self.setMouseTracking(True)
        self.is_drawing = False

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.is_drawing = True
            self._handle_mouse(event)

    def mouseMoveEvent(self, event):
        if self.is_drawing:
            self._handle_mouse(event)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.is_drawing = False
            self.studio.canvas.push_history()

    def _handle_mouse(self, event):
        pos = event.pos()
        lx = pos.x() // self.zoom
        ly = pos.y() // self.zoom

        if 0 <= lx < 320 and 0 <= ly < 240:
            tool = self.studio.active_tool
            color = self.studio.active_color
            size = self.studio.brush_size

            if tool == "pencil":
                self.studio.canvas.draw_pixel(lx, ly, color, size)
            elif tool == "eraser":
                self.studio.canvas.draw_pixel(lx, ly, "#000000", size)
            elif tool == "bucket":
                self.studio.canvas.flood_fill(lx, ly, color)
            elif tool == "eyedropper":
                picked = self.studio.canvas.get_pixel_color(lx, ly)
                self.studio.set_active_color(picked)

            self.studio.update_preview()


class TomTomFaceStudio(QMainWindow):
    """Main Application Window for TomTom Face Studio."""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("TomTom Face Studio - [TomTom ONE v6 / Nano-X 320x240]")
        self.resize(1100, 750)

        # Editor State
        self.canvas = CanvasModel("#000000")
        self.active_tool = "pencil"
        self.active_color = "#84EBFF"  # Luminous Cyan default
        self.brush_size = 1

        self.project_data = {
            "metadata": {
                "name": "Cyberpunk Neon",
                "version": "1.0.0",
                "author": "TomTom Owner"
            },
            "background_color": "#000000",
            "canvas": {"width": 320, "height": 240},
            "display": {
                "formats": ["horizontal"],
                "default_format": "horizontal",
            },
            "data_requirements": [
                "battery.percent",
                "gps.fix",
                "weather.current.temperature",
                "weather.current.condition",
            ],
            "elements": [
                {
                    "id": "time_main",
                    "type": "digital_time",
                    "format": "HH:MM",
                    "x": 60,
                    "y": 80,
                    "width": 200,
                    "height": 60,
                    "color": "#84EBFF",
                    "font_size": 48,
                    "is_12h": False,
                    "rule": None
                },
                {
                    "id": "date_sec",
                    "type": "date",
                    "x": 80,
                    "y": 145,
                    "width": 160,
                    "height": 24,
                    "color": "#14B4FF",
                    "font_size": 20,
                    "rule": None
                },
                {
                    "id": "battery_warn",
                    "type": "status_icon",
                    "icon_type": "battery_low",
                    "x": 285,
                    "y": 10,
                    "width": 24,
                    "height": 24,
                    "color": "#FF4444",
                    "rule": "battery_low"
                },
                {
                    "id": "gps_status",
                    "type": "status_icon",
                    "icon_type": "gps_fix",
                    "x": 10,
                    "y": 10,
                    "width": 24,
                    "height": 24,
                    "color": "#00FFCC",
                    "rule": "gps_fix"
                },
                {
                    "id": "weather_warn",
                    "type": "status_icon",
                    "icon_type": "weather_unavailable",
                    "x": 40,
                    "y": 10,
                    "width": 24,
                    "height": 24,
                    "color": "#FFAA00",
                    "rule": "weather_unavailable"
                }
            ],
            "rules": [
                {"name": "battery_low", "signal": "battery_percent", "op": "<=", "value": 20},
                {"name": "gps_fix", "signal": "gps_status", "op": "==", "value": 1},
                {"name": "weather_unavailable", "signal": "weather_status", "op": "==", "value": 0}
            ]
        }

        # Device Simulation State
        self.sim_state = {
            "battery_percent": 15,    # Low battery default to test rule
            "gps_status": 1,          # Fixed default
            "weather_status": 0,      # Unavailable default to test rule
            "charging": False
        }
        self.sim_time = datetime.now()
        self.is_live_clock = True
        self.frame_tick = 0

        # Build UI
        self._init_ui()

        # Timer for 1 Hz clock simulation
        self.timer = QTimer(self)
        self.timer.timeout.connect(self._on_clock_tick)
        self.timer.start(1000)

        self.update_preview()

    def _init_ui(self):
        # Central Widget & Main Splitter
        main_splitter = QSplitter(Qt.Horizontal, self)
        self.setCentralWidget(main_splitter)

        # --- Left Panel: Tools & Controls ---
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)

        # Drawing Tools Box
        tool_group = QGroupBox("Hand Drawing Tools")
        tool_layout = QGridLayout(tool_group)

        self.btn_pencil = QPushButton("Pencil")
        self.btn_eraser = QPushButton("Eraser")
        self.btn_bucket = QPushButton("Fill Bucket")
        self.btn_eyedropper = QPushButton("Eyedropper")
        self.btn_clear = QPushButton("Clear Canvas")

        self.btn_pencil.setCheckable(True)
        self.btn_eraser.setCheckable(True)
        self.btn_bucket.setCheckable(True)
        self.btn_eyedropper.setCheckable(True)
        self.btn_pencil.setChecked(True)

        self.btn_pencil.clicked.connect(lambda: self.set_tool("pencil"))
        self.btn_eraser.clicked.connect(lambda: self.set_tool("eraser"))
        self.btn_bucket.clicked.connect(lambda: self.set_tool("bucket"))
        self.btn_eyedropper.clicked.connect(lambda: self.set_tool("eyedropper"))
        self.btn_clear.clicked.connect(self._on_clear_canvas)

        tool_layout.addWidget(self.btn_pencil, 0, 0)
        tool_layout.addWidget(self.btn_eraser, 0, 1)
        tool_layout.addWidget(self.btn_bucket, 1, 0)
        tool_layout.addWidget(self.btn_eyedropper, 1, 1)
        tool_layout.addWidget(self.btn_clear, 2, 0, 1, 2)

        # Brush Size & Color Swatches
        color_layout = QHBoxLayout()
        color_layout.addWidget(QLabel("Brush Size:"))
        self.spin_brush = QSpinBox()
        self.spin_brush.setRange(1, 8)
        self.spin_brush.setValue(1)
        self.spin_brush.valueChanged.connect(self._on_brush_size_changed)
        color_layout.addWidget(self.spin_brush)

        self.btn_color_pick = QPushButton()
        self.btn_color_pick.setStyleSheet(f"background-color: {self.active_color}; border: 1px solid #ffffff;")
        self.btn_color_pick.clicked.connect(self._on_choose_color)
        color_layout.addWidget(QLabel("Color:"))
        color_layout.addWidget(self.btn_color_pick)

        tool_layout.addLayout(color_layout, 3, 0, 1, 2)

        # Palette Quick Buttons
        palette_layout = QHBoxLayout()
        swatches = ["#84EBFF", "#00F5FF", "#E8DAF2", "#FF7341", "#60A5FA", "#000000", "#FFFFFF", "#FF4444"]
        for c in swatches:
            btn = QPushButton()
            btn.setFixedSize(22, 22)
            btn.setStyleSheet(f"background-color: {c}; border: 1px solid #666666;")
            btn.clicked.connect(lambda _, col=c: self.set_active_color(col))
            palette_layout.addWidget(btn)
        tool_layout.addLayout(palette_layout, 4, 0, 1, 2)

        left_layout.addWidget(tool_group)

        # Undo / Redo Box
        undo_layout = QHBoxLayout()
        self.btn_undo = QPushButton("Undo")
        self.btn_redo = QPushButton("Redo")
        self.btn_undo.clicked.connect(self._on_undo)
        self.btn_redo.clicked.connect(self._on_redo)
        undo_layout.addWidget(self.btn_undo)
        undo_layout.addWidget(self.btn_redo)
        left_layout.addLayout(undo_layout)

        # Background Image Import Box
        import_group = QGroupBox("Import Background Artwork")
        import_layout = QVBoxLayout(import_group)
        btn_import_bg = QPushButton("Import PNG / JPG Artwork...")
        btn_import_bg.clicked.connect(self._on_import_artwork)
        import_layout.addWidget(btn_import_bg)

        fit_layout = QHBoxLayout()
        fit_layout.addWidget(QLabel("Fit Mode:"))
        self.combo_fit = QComboBox()
        self.combo_fit.addItems(["contain", "cover", "stretch", "custom"])
        fit_layout.addWidget(self.combo_fit)
        import_layout.addLayout(fit_layout)

        left_layout.addWidget(import_group)
        left_layout.addStretch()

        main_splitter.addWidget(left_panel)

        # --- Middle Panel: Display Canvas & View Controls ---
        mid_panel = QWidget()
        mid_layout = QVBoxLayout(mid_panel)

        zoom_bar = QHBoxLayout()
        zoom_bar.addWidget(QLabel("Zoom:"))
        self.combo_zoom = QComboBox()
        self.combo_zoom.addItems(["1x (320x240)", "2x (640x480)", "3x (960x720)", "4x (1280x960)"])
        self.combo_zoom.setCurrentIndex(1)
        self.combo_zoom.currentIndexChanged.connect(self._on_zoom_changed)
        zoom_bar.addWidget(self.combo_zoom)

        self.chk_grid = QCheckBox("Show Pixel Grid")
        self.chk_grid.setChecked(True)
        self.chk_grid.stateChanged.connect(self.update_preview)
        zoom_bar.addWidget(self.chk_grid)
        zoom_bar.addStretch()

        mid_layout.addLayout(zoom_bar)

        self.canvas_widget = CanvasWidget(self)
        self.canvas_widget.setFrameShape(QFrame.Box)
        self.canvas_widget.setAlignment(Qt.AlignCenter)
        mid_layout.addWidget(self.canvas_widget, 1)

        main_splitter.addWidget(mid_panel)

        # --- Right Panel: Tabs for Elements, Rules, Simulator, Export ---
        right_panel = QTabWidget()

        # Tab 1: Elements
        tab_elements = QWidget()
        el_layout = QVBoxLayout(tab_elements)

        self.list_elements = QListWidget()
        self.list_elements.currentRowChanged.connect(self._on_element_selected)
        el_layout.addWidget(QLabel("Layout Elements:"))
        el_layout.addWidget(self.list_elements)

        btn_add_layout = QHBoxLayout()
        btn_add_time = QPushButton("+ Time")
        btn_add_date = QPushButton("+ Date")
        btn_add_text = QPushButton("+ Text")
        btn_add_icon = QPushButton("+ Icon")

        btn_add_time.clicked.connect(lambda: self.add_element("digital_time"))
        btn_add_date.clicked.connect(lambda: self.add_element("date"))
        btn_add_text.clicked.connect(lambda: self.add_element("text"))
        btn_add_icon.clicked.connect(lambda: self.add_element("status_icon"))

        btn_add_layout.addWidget(btn_add_time)
        btn_add_layout.addWidget(btn_add_date)
        btn_add_layout.addWidget(btn_add_text)
        btn_add_layout.addWidget(btn_add_icon)
        el_layout.addLayout(btn_add_layout)

        # Selected Element Property Editor
        prop_group = QGroupBox("Element Properties")
        prop_layout = QFormLayout_Helper(prop_group)

        self.edit_el_id = QLineEdit()
        self.spin_el_x = QSpinBox()
        self.spin_el_x.setRange(0, 320)
        self.spin_el_y = QSpinBox()
        self.spin_el_y.setRange(0, 240)

        self.btn_el_color = QPushButton()
        self.btn_el_color.clicked.connect(self._on_choose_element_color)

        self.spin_el_font = QSpinBox()
        self.spin_el_font.setRange(8, 96)

        self.combo_el_rule = QComboBox()
        self.combo_el_rule.addItems(["None", "battery_low", "gps_fix", "weather_unavailable"])

        self.edit_el_id.textChanged.connect(self._on_element_prop_changed)
        self.spin_el_x.valueChanged.connect(self._on_element_prop_changed)
        self.spin_el_y.valueChanged.connect(self._on_element_prop_changed)
        self.spin_el_font.valueChanged.connect(self._on_element_prop_changed)
        self.combo_el_rule.currentIndexChanged.connect(self._on_element_prop_changed)

        prop_layout.addRow("Element ID:", self.edit_el_id)
        prop_layout.addRow("Pos X:", self.spin_el_x)
        prop_layout.addRow("Pos Y:", self.spin_el_y)
        prop_layout.addRow("Font Size:", self.spin_el_font)
        prop_layout.addRow("Color:", self.btn_el_color)
        prop_layout.addRow("Rule Binding:", self.combo_el_rule)

        btn_del_el = QPushButton("Delete Element")
        btn_del_el.setStyleSheet("color: red;")
        btn_del_el.clicked.connect(self._on_delete_element)
        prop_layout.addRow(btn_del_el)

        el_layout.addWidget(prop_group)
        right_panel.addTab(tab_elements, "Elements")

        # Tab 2: Simulator & Rules
        tab_sim = QWidget()
        sim_layout = QVBoxLayout(tab_sim)

        sim_group = QGroupBox("Device Hardware State Simulator [SIMULATED DEVICE DATA]")
        sim_form = QVBoxLayout(sim_group)

        sim_notice = QLabel("[SIMULATED DATA ONLY - Never presented as live device telemetry]")
        sim_notice.setStyleSheet("color: #FFAA00; font-weight: bold;")
        sim_form.addWidget(sim_notice)

        # Battery Slider
        bat_box = QHBoxLayout()
        bat_box.addWidget(QLabel("Battery Level [SIMULATED]:"))
        self.slider_bat = QSlider(Qt.Horizontal)
        self.slider_bat.setRange(0, 100)
        self.slider_bat.setValue(self.sim_state["battery_percent"])
        self.lbl_bat = QLabel(f"{self.sim_state['battery_percent']}%")
        self.slider_bat.valueChanged.connect(self._on_sim_bat_changed)
        bat_box.addWidget(self.slider_bat)
        bat_box.addWidget(self.lbl_bat)
        sim_form.addLayout(bat_box)

        # GPS Toggle
        self.chk_gps = QCheckBox("GPS Satellite Fix Acquired [SIMULATED] (gps_fix)")
        self.chk_gps.setChecked(bool(self.sim_state["gps_status"]))
        self.chk_gps.stateChanged.connect(self._on_sim_gps_changed)
        sim_form.addWidget(self.chk_gps)

        # Weather Toggle
        self.chk_weather = QCheckBox("Weather Telemetry Offline [SIMULATED] (weather_unavailable)")
        self.chk_weather.setChecked(self.sim_state["weather_status"] == 0)
        self.chk_weather.stateChanged.connect(self._on_sim_weather_changed)
        sim_form.addWidget(self.chk_weather)

        sim_layout.addWidget(sim_group)
        sim_layout.addStretch()
        right_panel.addTab(tab_sim, "Simulator")

        # Tab 3: Data Add-ons & Privacy
        tab_data_privacy = QWidget()
        data_privacy_layout = QVBoxLayout(tab_data_privacy)

        data_group = QGroupBox("Face-Declared Data Requirements")
        data_box = QVBoxLayout(data_group)
        data_box.addWidget(QLabel("Select data add-on fields required by this face:"))

        self.data_req_checks = {}
        curr_reqs = set(self.project_data.get("data_requirements", []))
        for field_id, info in SUPPORTED_DATA_FIELDS.items():
            chk = QCheckBox(f"{field_id} ({info['description']})")
            chk.setChecked(field_id in curr_reqs)
            chk.toggled.connect(self._on_data_reqs_changed)
            self.data_req_checks[field_id] = chk
            data_box.addWidget(chk)

        data_privacy_layout.addWidget(data_group)

        privacy_group = QGroupBox("Privacy & Network Settings")
        privacy_box = QVBoxLayout(privacy_group)

        self.chk_location_sharing = QCheckBox("Enable Location Sharing for Weather Service")
        self.chk_location_sharing.setChecked(False)
        self.chk_location_sharing.toggled.connect(self._on_location_sharing_changed)
        privacy_box.addWidget(self.chk_location_sharing)

        notice_lbl = QLabel(
            "Privacy Notice: Telemetry location sharing is disabled by default.\n"
            "GPS coordinates are never stored in cache, logged, or transmitted\n"
            "without explicit user opt-in."
        )
        notice_lbl.setStyleSheet("color: #60A5FA; font-style: italic;")
        privacy_box.addWidget(notice_lbl)

        data_privacy_layout.addWidget(privacy_group)

        cache_group = QGroupBox("Weather Cache Contract [SIMULATED / DEVICE STATUS]")
        cache_box = QVBoxLayout(cache_group)
        cache_box.addWidget(QLabel("Device GPS Fix: Unavailable (TTY SAC1 NMEA offline)"))
        cache_box.addWidget(QLabel("Cache Size Limit: Strict <= 4 KiB max enforced"))
        cache_box.addWidget(QLabel("Refresh Limiter: Max 1 fetch per hour"))
        data_privacy_layout.addWidget(cache_group)

        data_privacy_layout.addStretch()
        right_panel.addTab(tab_data_privacy, "Data & Privacy")

        # Tab 3: Package Exporter
        tab_export = QWidget()
        exp_layout = QVBoxLayout(tab_export)

        meta_group = QGroupBox("Package Metadata")
        meta_form = QFormLayout_Helper(meta_group)

        self.edit_proj_name = QLineEdit(self.project_data["metadata"]["name"])
        self.edit_proj_author = QLineEdit(self.project_data["metadata"]["author"])
        self.edit_proj_ver = QLineEdit(self.project_data["metadata"]["version"])
        self.edit_gallery_name = QLineEdit("My TomTom Faces")
        self.format_checks = {}
        self._updating_formats = False
        formats_group = QGroupBox("Formats Supported by This Face")
        formats_layout = QVBoxLayout(formats_group)
        current_formats, default_format = get_project_formats(self.project_data)
        for format_id, definition in FORMAT_REGISTRY.items():
            checkbox = QCheckBox(f"{definition['name']} ({format_id})")
            checkbox.setChecked(format_id in current_formats)
            checkbox.toggled.connect(self._on_face_formats_changed)
            self.format_checks[format_id] = checkbox
            formats_layout.addWidget(checkbox)
        self.combo_default_format = QComboBox()
        self.combo_default_format.currentIndexChanged.connect(
            self._on_default_format_changed
        )
        formats_layout.addWidget(QLabel("Default format:"))
        formats_layout.addWidget(self.combo_default_format)
        self._refresh_default_format_choices(default_format)

        meta_form.addRow("Face Name:", self.edit_proj_name)
        meta_form.addRow("Author:", self.edit_proj_author)
        meta_form.addRow("Version:", self.edit_proj_ver)
        meta_form.addRow("Gallery Name:", self.edit_gallery_name)
        exp_layout.addWidget(meta_group)
        exp_layout.addWidget(formats_group)

        btn_validate = QPushButton("Validate Project Schema")
        btn_validate.clicked.connect(self._on_validate_project)
        exp_layout.addWidget(btn_validate)

        btn_export = QPushButton("Export .ttface Package...")
        btn_export.setStyleSheet("background-color: #007ACC; color: white; font-weight: bold; padding: 8px;")
        btn_export.clicked.connect(self._on_export_package)
        exp_layout.addWidget(btn_export)

        btn_export_gallery = QPushButton("Build .ttgallery Collection...")
        btn_export_gallery.setToolTip(
            "Bundle this face with selected .ttface packages and previews."
        )
        btn_export_gallery.clicked.connect(self._on_export_gallery)
        exp_layout.addWidget(btn_export_gallery)

        exp_layout.addStretch()
        right_panel.addTab(tab_export, "Export")

        tab_device = QWidget()
        device_layout = QVBoxLayout(tab_device)
        device_group = QGroupBox("Connected TomTom Face")
        device_form = QFormLayout_Helper(device_group)
        self.edit_device_host = QLineEdit("192.168.101.115")
        self.combo_device_face = QComboBox()
        for face_id, face_name in DEVICE_FACE_OPTIONS:
            self.combo_device_face.addItem(face_name, face_id)
        self.combo_device_face.setCurrentIndex(
            self.combo_device_face.findData(7)
        )
        self.btn_apply_device_face = QPushButton("Make This the Active Face")
        self.btn_apply_device_face.clicked.connect(self._on_apply_device_face)
        self.btn_read_device_face = QPushButton("Read Current Device Face")
        self.btn_read_device_face.clicked.connect(self._on_read_device_face)
        self.lbl_device_status = QLabel("Connect the TomTom directly by USB Ethernet.")
        self.lbl_device_status.setWordWrap(True)
        device_form.addRow("TomTom USB IP:", self.edit_device_host)
        device_form.addRow("Current face:", self.combo_device_face)
        device_form.addRow(self.btn_apply_device_face)
        device_form.addRow(self.btn_read_device_face)
        device_form.addRow(self.lbl_device_status)
        device_layout.addWidget(device_group)
        warning = QLabel(
            "Face Studio uses a small fixed-command service on TCP port 18743, "
            "bound only to the TomTom's USB Ethernet address. Keep the USB "
            "link direct; do not bridge or expose it to other networks."
        )
        warning.setWordWrap(True)
        warning.setStyleSheet("color: #D97706;")
        device_layout.addWidget(warning)
        device_layout.addStretch()
        right_panel.addTab(tab_device, "Device")

        main_splitter.addWidget(right_panel)
        main_splitter.setSizes([250, 550, 300])

        self._populate_elements_list()

    def set_tool(self, tool_name: str):
        self.active_tool = tool_name
        self.btn_pencil.setChecked(tool_name == "pencil")
        self.btn_eraser.setChecked(tool_name == "eraser")
        self.btn_bucket.setChecked(tool_name == "bucket")
        self.btn_eyedropper.setChecked(tool_name == "eyedropper")

    def set_active_color(self, color_hex: str):
        self.active_color = color_hex
        self.btn_color_pick.setStyleSheet(f"background-color: {color_hex}; border: 1px solid #ffffff;")

    def _on_choose_color(self):
        c = QColorDialog.getColor(QColor(self.active_color), self, "Select Active Brush Color")
        if c.isValid():
            self.set_active_color(c.name())

    def _on_brush_size_changed(self, val):
        self.brush_size = val

    def _on_clear_canvas(self):
        self.canvas.clear("#000000")
        self.update_preview()

    def _on_undo(self):
        if self.canvas.undo():
            self.update_preview()

    def _on_redo(self):
        if self.canvas.redo():
            self.update_preview()

    def _on_zoom_changed(self, idx):
        self.canvas_widget.zoom = idx + 1
        self.update_preview()

    def _on_clock_tick(self):
        self.sim_time = datetime.now()
        self.frame_tick = (self.frame_tick + 1) % 100
        self.update_preview()

    def _on_sim_bat_changed(self, val):
        self.sim_state["battery_percent"] = val
        self.lbl_bat.setText(f"{val}%")
        self.update_preview()

    def _on_sim_gps_changed(self, state):
        self.sim_state["gps_status"] = 1 if state == Qt.Checked else 0
        self.update_preview()

    def _on_sim_weather_changed(self, state):
        self.sim_state["weather_status"] = 0 if state == Qt.Checked else 1
        self.update_preview()

    def _on_data_reqs_changed(self, _checked):
        reqs = [
            field_id for field_id, chk in self.data_req_checks.items()
            if chk.isChecked()
        ]
        self.project_data["data_requirements"] = reqs

    def _on_location_sharing_changed(self, checked):
        SharedWeatherService.set_location_sharing(checked)

    def _on_apply_device_face(self):
        self.btn_apply_device_face.setEnabled(False)
        self.btn_read_device_face.setEnabled(False)
        self.lbl_device_status.setText("Applying face over the USB connection…")
        QApplication.processEvents()
        try:
            select_device_face(
                self.edit_device_host.text(),
                self.combo_device_face.currentData(),
            )
        except (DeviceControlError, ValueError) as error:
            self.lbl_device_status.setText(str(error))
            QMessageBox.warning(self, "TomTom Face Selection", str(error))
        else:
            self.lbl_device_status.setText(
                f"{self.combo_device_face.currentText()} is selected. "
                "The TomTom applies it within about one second and keeps it "
                "as the default after restart."
            )
        finally:
            self.btn_apply_device_face.setEnabled(True)
            self.btn_read_device_face.setEnabled(True)

    def _on_read_device_face(self):
        self.btn_apply_device_face.setEnabled(False)
        self.btn_read_device_face.setEnabled(False)
        try:
            face_id = read_device_face(self.edit_device_host.text())
        except (DeviceControlError, ValueError) as error:
            self.lbl_device_status.setText(str(error))
            QMessageBox.warning(self, "TomTom Face Status", str(error))
        else:
            face_name = dict(DEVICE_FACE_OPTIONS)[face_id]
            self.combo_device_face.setCurrentIndex(
                self.combo_device_face.findData(face_id)
            )
            self.lbl_device_status.setText(
                f"{face_name} is selected on the connected TomTom."
            )
        finally:
            self.btn_apply_device_face.setEnabled(True)
            self.btn_read_device_face.setEnabled(True)

    def _populate_elements_list(self):
        self.list_elements.clear()
        for el in self.project_data.get("elements", []):
            label = f"[{el.get('type')}] {el.get('id')} (x={el.get('x')}, y={el.get('y')})"
            self.list_elements.addItem(QListWidgetItem(label))

    def _on_element_selected(self, row):
        elements = self.project_data.get("elements", [])
        if 0 <= row < len(elements):
            el = elements[row]
            self.edit_el_id.blockSignals(True)
            self.spin_el_x.blockSignals(True)
            self.spin_el_y.blockSignals(True)
            self.spin_el_font.blockSignals(True)
            self.combo_el_rule.blockSignals(True)

            self.edit_el_id.setText(el.get("id", ""))
            self.spin_el_x.setValue(el.get("x", 0))
            self.spin_el_y.setValue(el.get("y", 0))
            self.spin_el_font.setValue(el.get("font_size", 24))

            self.btn_el_color.setStyleSheet(f"background-color: {el.get('color', '#FFFFFF')};")

            rule_val = el.get("rule") or "None"
            idx = self.combo_el_rule.findText(rule_val)
            if idx >= 0:
                self.combo_el_rule.setCurrentIndex(idx)

            self.edit_el_id.blockSignals(False)
            self.spin_el_x.blockSignals(False)
            self.spin_el_y.blockSignals(False)
            self.spin_el_font.blockSignals(False)
            self.combo_el_rule.blockSignals(False)

    def _on_element_prop_changed(self):
        row = self.list_elements.currentRow()
        elements = self.project_data.get("elements", [])
        if 0 <= row < len(elements):
            el = elements[row]
            el["id"] = self.edit_el_id.text()
            el["x"] = self.spin_el_x.value()
            el["y"] = self.spin_el_y.value()
            el["font_size"] = self.spin_el_font.value()

            rule_sel = self.combo_el_rule.currentText()
            el["rule"] = None if rule_sel == "None" else rule_sel

            self._populate_elements_list()
            self.list_elements.setCurrentRow(row)
            self.update_preview()

    def _on_choose_element_color(self):
        row = self.list_elements.currentRow()
        elements = self.project_data.get("elements", [])
        if 0 <= row < len(elements):
            c = QColorDialog.getColor(QColor(elements[row].get("color", "#FFFFFF")), self, "Select Element Color")
            if c.isValid():
                elements[row]["color"] = c.name()
                self.btn_el_color.setStyleSheet(f"background-color: {c.name()};")
                self.update_preview()

    def add_element(self, el_type: str):
        elements = self.project_data.setdefault("elements", [])
        idx = len(elements) + 1
        new_el = {
            "id": f"{el_type}_{idx}",
            "type": el_type,
            "x": 50,
            "y": 50,
            "width": 100,
            "height": 40,
            "color": "#84EBFF",
            "font_size": 24,
            "rule": None
        }
        if el_type == "status_icon":
            new_el["icon_type"] = "battery_low"
            new_el["width"] = 24
            new_el["height"] = 24
            new_el["rule"] = "battery_low"

        elements.append(new_el)
        self._populate_elements_list()
        self.list_elements.setCurrentRow(len(elements) - 1)
        self.update_preview()

    def _on_delete_element(self):
        row = self.list_elements.currentRow()
        elements = self.project_data.get("elements", [])
        if 0 <= row < len(elements):
            elements.pop(row)
            self._populate_elements_list()
            self.update_preview()

    def _on_import_artwork(self):
        file_path, _ = QFileDialog.getOpenFileName(self, "Import Background Artwork", "", "Image Files (*.png *.jpg *.jpeg *.bmp)")
        if file_path:
            try:
                img = Image.open(file_path)
                mode = self.combo_fit.currentText()
                self.canvas.import_background(img, mode=mode)
                self.update_preview()
            except Exception as e:
                QMessageBox.critical(self, "Import Failed", f"Could not import image: {str(e)}")

    def update_preview(self):
        """Render composite frame (Canvas Drawing + Visible Elements) to QLabel."""
        comp_img = self.canvas.image.copy()
        draw = ImageDraw.Draw(comp_img)

        # Render elements considering simulator state & rules
        rules_def = self.project_data.get("rules", [])
        for el in self.project_data.get("elements", []):
            if RuleEngine.is_element_visible(el, rules_def, self.sim_state):
                preview_element = el
                if el.get("type") == "digital_time":
                    preview_element = dict(el)
                    _formats, default_format = get_project_formats(
                        self.project_data
                    )
                    preview_element["layout"] = (
                        "stacked" if default_format == "stacked" else "horizontal"
                    )
                render_element(draw, preview_element, self.sim_time,
                               self.frame_tick)

        # Scale image according to zoom
        zoom = self.canvas_widget.zoom
        sw, sh = 320 * zoom, 240 * zoom
        scaled_img = comp_img.resize((sw, sh), Image.Resampling.NEAREST)

        # Draw grid lines if zoom >= 4x and grid option checked
        if zoom >= 4 and self.chk_grid.isChecked():
            draw_grid = ImageDraw.Draw(scaled_img)
            for x in range(0, sw, zoom):
                draw_grid.line([(x, 0), (x, sh)], fill=(40, 40, 40))
            for y in range(0, sh, zoom):
                draw_grid.line([(0, y), (sw, y)], fill=(40, 40, 40))

        # Convert PIL Image to QPixmap
        data = scaled_img.tobytes("raw", "RGB")
        qimg = QImage(data, sw, sh, sw * 3, QImage.Format_RGB888)
        pix = QPixmap.fromImage(qimg)
        self.canvas_widget.setPixmap(pix)

    def _on_validate_project(self):
        self.project_data["metadata"]["name"] = self.edit_proj_name.text()
        self.project_data["metadata"]["author"] = self.edit_proj_author.text()
        self.project_data["metadata"]["version"] = self.edit_proj_ver.text()

        errors = validate_face_project(self.project_data)
        if not errors:
            QMessageBox.information(self, "Validation Passed", "Project passed all 320x240 TomTom ONE schema checks cleanly!")
        else:
            msg = "\n".join(errors)
            QMessageBox.warning(self, "Validation Issues Found", f"Project Validation Results:\n\n{msg}")

    def _selected_face_formats(self):
        return [
            format_id for format_id, checkbox in self.format_checks.items()
            if checkbox.isChecked()
        ]

    def _refresh_default_format_choices(self, preferred=""):
        formats = self._selected_face_formats()
        previous = preferred or self.combo_default_format.currentData()
        self._updating_formats = True
        self.combo_default_format.clear()
        for format_id in formats:
            definition = FORMAT_REGISTRY[format_id]
            self.combo_default_format.addItem(
                f"{definition['name']} ({format_id})", format_id
            )
        selected = self.combo_default_format.findData(previous)
        if selected < 0 and formats:
            selected = 0
        if selected >= 0:
            self.combo_default_format.setCurrentIndex(selected)
        self.combo_default_format.setEnabled(bool(formats))
        self._updating_formats = False
        display = self.project_data.setdefault("display", {})
        display["formats"] = formats
        display["default_format"] = (
            self.combo_default_format.currentData() if formats else None
        )

    def _on_face_formats_changed(self, _checked):
        preferred = self.project_data.get("display", {}).get(
            "default_format", ""
        )
        self._refresh_default_format_choices(preferred)
        self.update_preview()

    def _on_default_format_changed(self, _index):
        if self._updating_formats:
            return
        display = self.project_data.setdefault("display", {})
        display["formats"] = self._selected_face_formats()
        display["default_format"] = self.combo_default_format.currentData()
        self.update_preview()

    def _on_export_package(self):
        self._sync_package_metadata()
        save_path, _ = QFileDialog.getSaveFileName(
            self,
            "Export .ttface Package",
            f"{self.edit_proj_name.text().lower().replace(' ', '_')}.ttface",
            "TomTom Face Package (*.ttface)",
        )
        if save_path:
            success, msgs = export_ttface_package(
                self.project_data, self.canvas.image, save_path
            )
            if success:
                QMessageBox.information(self, "Export Successful", f"Exported package cleanly to:\n{save_path}")
            else:
                msg = "\n".join(msgs)
                QMessageBox.critical(self, "Export Failed", f"Export errors:\n\n{msg}")

    def _sync_package_metadata(self):
        self.project_data["metadata"]["name"] = self.edit_proj_name.text()
        self.project_data["metadata"]["author"] = self.edit_proj_author.text()
        self.project_data["metadata"]["version"] = self.edit_proj_ver.text()
        display = self.project_data.setdefault("display", {})
        display["formats"] = self._selected_face_formats()
        display["default_format"] = self.combo_default_format.currentData()
        reqs = [
            field_id for field_id, chk in getattr(self, "data_req_checks", {}).items()
            if chk.isChecked()
        ]
        if reqs:
            self.project_data["data_requirements"] = reqs

    def _on_export_gallery(self):
        self._sync_package_metadata()
        other_faces, _ = QFileDialog.getOpenFileNames(
            self,
            "Add Existing Face Packages (optional)",
            "",
            "TomTom Face Packages (*.ttface)",
        )
        save_path, _ = QFileDialog.getSaveFileName(
            self,
            "Build .ttgallery Collection",
            f"{self.edit_gallery_name.text().lower().replace(' ', '_')}.ttgallery",
            "TomTom Face Gallery (*.ttgallery)",
        )
        if not save_path:
            return
        try:
            with tempfile.TemporaryDirectory(prefix="tomtom-face-gallery-") as temp_dir:
                current_face = os.path.join(temp_dir, "current.ttface")
                success, messages = export_ttface_package(
                    self.project_data, self.canvas.image, current_face
                )
                if not success:
                    raise ValueError("\n".join(messages))
                success, messages = export_ttgallery(
                    [current_face] + other_faces,
                    save_path,
                    self.edit_gallery_name.text(),
                )
                if not success:
                    raise ValueError("\n".join(messages))
            QMessageBox.information(
                self, "Gallery Built",
                f"Created gallery with {1 + len(other_faces)} face(s):\n{save_path}\n\n"
                "Rebuild the gallery from the desired face list to reflect additions and removals.",
            )
        except (OSError, ValueError) as error:
            QMessageBox.critical(self, "Gallery Build Failed", str(error))


class QFormLayout_Helper(QVBoxLayout):
    """Simple form layout helper using QVBoxLayout."""
    def addRow(self, label_text_or_widget, widget=None):
        box = QHBoxLayout()
        if isinstance(label_text_or_widget, str):
            lbl = QLabel(label_text_or_widget)
            lbl.setFixedWidth(100)
            box.addWidget(lbl)
            if widget:
                box.addWidget(widget)
        else:
            box.addWidget(label_text_or_widget)
        self.addLayout(box)


def main():
    app = QApplication(sys.argv)
    window = TomTomFaceStudio()
    window.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
