#ifndef DISPLAY_H
#define DISPLAY_H

#include <lvgl.h>
#include <TFT_eSPI.h>

// Fortschrittsbalken-Typen
enum ProgressBarType {
  BAR_HORIZONTAL,
  BAR_VERTICAL,
  BAR_CIRCULAR
};

/**
 * Hilfsfunktion zum Erstellen eines ansprechenden Fortschrittsbalkens mit LVGL
 * 
 * @param parent Elternobjekt im LVGL-Objektbaum
 * @param x X-Position des Balkens
 * @param y Y-Position des Balkens
 * @param width Breite des Balkens
 * @param height Höhe des Balkens
 * @param initial_value Anfangswert (0-100)
 * @return Pointer auf den erstellten Fortschrittsbalken
 */
lv_obj_t* createStyledProgressBar(lv_obj_t* parent, int16_t x, int16_t y, int16_t width, int16_t height, int16_t initial_value) {
    // Fortschrittsbalken erstellen
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_size(bar, width, height);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, x, y);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, initial_value, LV_ANIM_OFF);
    
    // Style für den Hintergrund
    static lv_style_t style_bg;
    lv_style_init(&style_bg);
    lv_style_set_border_width(&style_bg, 2);
    lv_style_set_border_color(&style_bg, lv_color_hex(0x555555));
    lv_style_set_pad_all(&style_bg, 3);
    lv_style_set_radius(&style_bg, 6);
    lv_style_set_bg_color(&style_bg, lv_color_hex(0x333333));
    
    // Style für den Indikator (Fortschritt)
    static lv_style_t style_indic;
    lv_style_init(&style_indic);
    lv_style_set_bg_color(&style_indic, lv_color_hex(0x00DDDD)); // Türkis
    lv_style_set_bg_grad_color(&style_indic, lv_color_hex(0x00AAAA));
    lv_style_set_bg_grad_dir(&style_indic, LV_GRAD_DIR_HOR);
    lv_style_set_radius(&style_indic, 3);
    
    // Styles anwenden
    lv_obj_add_style(bar, &style_bg, LV_PART_MAIN);
    lv_obj_add_style(bar, &style_indic, LV_PART_INDICATOR);
    
    return bar;
}

/**
 * Erstellt einen modern gestalteten Button mit Icon
 * 
 * @param parent Elternobjekt
 * @param x X-Position
 * @param y Y-Position
 * @param width Breite
 * @param height Höhe
 * @param text Buttontext
 * @param icon_symbol LV_SYMBOL_* für Button-Icon (oder NULL für keines)
 * @return Pointer auf den erstellten Button
 */
lv_obj_t* createStyledButton(lv_obj_t* parent, int16_t x, int16_t y, int16_t width, int16_t height, 
                            const char* text, const char* icon_symbol) {
    // Button erstellen
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, height);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, x, y);
    
    // Style für den Button
    static lv_style_t style_btn;
    lv_style_init(&style_btn);
    lv_style_set_radius(&style_btn, 10);
    lv_style_set_bg_color(&style_btn, lv_color_hex(0x005577));
    lv_style_set_bg_grad_color(&style_btn, lv_color_hex(0x007799));
    lv_style_set_bg_grad_dir(&style_btn, LV_GRAD_DIR_VER);
    lv_style_set_shadow_width(&style_btn, 5);
    lv_style_set_shadow_color(&style_btn, lv_color_hex(0x000000));
    lv_style_set_shadow_opa(&style_btn, LV_OPA_30);
    
    // Style für den gedrückten Zustand
    static lv_style_t style_btn_pressed;
    lv_style_init(&style_btn_pressed);
    lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(0x004466));
    lv_style_set_bg_grad_color(&style_btn_pressed, lv_color_hex(0x006688));
    lv_style_set_shadow_width(&style_btn_pressed, 2);
    
    // Styles anwenden
    lv_obj_add_style(btn, &style_btn, LV_PART_MAIN);
    lv_obj_add_style(btn, &style_btn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    
    // Container für Icon + Text mit flexibler Anordnung
    lv_obj_t* cont = lv_obj_create(btn);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, width - 10, height - 10);
    lv_obj_center(cont);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Icon hinzufügen, wenn angegeben
    if (icon_symbol != NULL) {
        lv_obj_t* icon = lv_label_create(cont);
        lv_label_set_text(icon, icon_symbol);
        lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    // Text hinzufügen
    lv_obj_t* label = lv_label_create(cont);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    return btn;
}

/**
 * Erstellt eine Uhrzeit/Datumsanzeige mit formatiertem Text
 * 
 * @param parent Elternobjekt
 * @param x X-Position
 * @param y Y-Position
 * @param width Breite
 * @param use_calendar Ob ein Kalender-Symbol angezeigt werden soll
 * @return Pointer auf das erstellte Label
 */
lv_obj_t* createTimeDisplay(lv_obj_t* parent, int16_t x, int16_t y, int16_t width, bool use_calendar) {
    // Container für Zeit/Datum
    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, width, LV_SIZE_CONTENT);
    lv_obj_align(cont, LV_ALIGN_TOP_LEFT, x, y);
    
    // Symbol hinzufügen
    if (use_calendar) {
        lv_obj_t* symbol = lv_label_create(cont);
        lv_label_set_text(symbol, LV_SYMBOL_CALENDAR);
        lv_obj_align(symbol, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_text_color(symbol, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    // Zeitlabel erstellen
    lv_obj_t* label = lv_label_create(cont);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    if (use_calendar) {
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 30, 0);
    } else {
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    }
    
    return label;
}

/**
 * Erstellt einen thematischen Bildschirmtitel mit Untertitel
 * 
 * @param parent Elternobjekt
 * @param title Haupttitel
 * @param subtitle Untertitel (kann NULL sein)
 * @return Container mit dem Titel
 */
lv_obj_t* createScreenTitle(lv_obj_t* parent, const char* title, const char* subtitle) {
    // Container für Titel
    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 10);
    
    // Haupttitel
    lv_obj_t* title_label = lv_label_create(cont);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);
    
    // Untertitel, falls vorhanden
    if (subtitle != NULL) {
        lv_obj_t* subtitle_label = lv_label_create(cont);
        lv_label_set_text(subtitle_label, subtitle);
        lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(subtitle_label, LV_ALIGN_TOP_MID, 0, 40);
    }
    
    // Trennlinie
    lv_obj_t* line = lv_line_create(cont);
    static lv_point_t line_points[] = {{0, 0}, {750, 0}};
    lv_line_set_points(line, line_points, 2);
    lv_obj_set_style_line_width(line, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(line, lv_color_hex(0x00DDDD), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    if (subtitle != NULL) {
        lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 70);
    } else {
        lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 40);
    }
    
    return cont;
}

/**
 * Erstellt einen Statusindikator (für Tankfüllstand, Verbindungsstatus, etc.)
 * 
 * @param parent Elternobjekt
 * @param x X-Position
 * @param y Y-Position
 * @param label Beschriftung
 * @param init_state Anfangsstatus (0: Fehler, 1: OK, 2: Warnung)
 * @return Pointer auf den Container
 */
lv_obj_t* createStatusIndicator(lv_obj_t* parent, int16_t x, int16_t y, const char* label, int init_state) {
    // Container für den Statusindikator
    lv_obj_t* cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, 160, 50);
    lv_obj_align(cont, LV_ALIGN_TOP_LEFT, x, y);
    
    // Beschriftung
    lv_obj_t* label_obj = lv_label_create(cont);
    lv_label_set_text(label_obj, label);
    lv_obj_set_style_text_color(label_obj, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_obj, LV_ALIGN_TOP_LEFT, 0, 0);
    
    // Status-Icon erstellen
    lv_obj_t* icon = lv_obj_create(cont);
    lv_obj_set_size(icon, 20, 20);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 10);
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    // Status-Text
    lv_obj_t* status_text = lv_label_create(cont);
    lv_obj_align(status_text, LV_ALIGN_LEFT_MID, 30, 10);
    
    // Status setzen
    if (init_state == 0) { // Fehler
        lv_obj_set_style_bg_color(icon, lv_color_hex(0xFF4444), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(status_text, "Fehler");
        lv_obj_set_style_text_color(status_text, lv_color_hex(0xFF4444), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if (init_state == 1) { // OK
        lv_obj_set_style_bg_color(icon, lv_color_hex(0x44FF44), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(status_text, "OK");
        lv_obj_set_style_text_color(status_text, lv_color_hex(0x44FF44), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else { // Warnung
        lv_obj_set_style_bg_color(icon, lv_color_hex(0xFFFF44), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(status_text, "Warnung");
        lv_obj_set_style_text_color(status_text, lv_color_hex(0xFFFF44), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    
    return cont;
}

#endif // DISPLAY_H