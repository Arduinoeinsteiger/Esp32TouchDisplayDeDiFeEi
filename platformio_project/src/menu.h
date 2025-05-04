#ifndef MENU_H
#define MENU_H

#include <Arduino.h>
#include <lvgl.h>

/**
 * Erstellt eine moderne Menüstruktur für LVGL
 */
class MenuSystem {
private:
  lv_obj_t* parent;     // Übergeordnetes Objekt für das Menü
  lv_obj_t* menuCont;   // Container für das Menü
  lv_obj_t* titleLabel; // Titel des Menüs
  
  // Callback-Typen
  typedef void (*MenuCallback)();
  typedef void (*ParamMenuCallback)(int);
  
  // Struktur für Menüeinträge
  struct MenuItem {
    String text;
    MenuCallback callback;
    ParamMenuCallback paramCallback;
    int param;
    bool hasParam;
  };
  
  // Menüeinträge
  std::vector<MenuItem> items;
  
  // Visuelle Einstellungen
  lv_style_t styleMenuItem;
  lv_style_t styleMenuItemSelected;
  lv_style_t styleMenuContainer;
  lv_style_t styleMenuTitle;

public:
  /**
   * Erstellt ein neues Menüsystem.
   * 
   * @param parent Das übergeordnete LVGL-Objekt
   * @param title Der Titel des Menüs
   * @param x X-Position des Menüs
   * @param y Y-Position des Menüs
   * @param width Breite des Menüs
   */
  MenuSystem(lv_obj_t* parent, const char* title, int16_t x, int16_t y, int16_t width) {
    this->parent = parent;
    
    // Styles initialisieren
    initStyles();
    
    // Menü-Container erstellen
    menuCont = lv_obj_create(parent);
    lv_obj_remove_style_all(menuCont);
    lv_obj_add_style(menuCont, &styleMenuContainer, 0);
    lv_obj_set_size(menuCont, width, LV_SIZE_CONTENT);
    lv_obj_set_pos(menuCont, x, y);
    lv_obj_set_layout(menuCont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(menuCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menuCont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(menuCont, 8, 0);
    lv_obj_set_style_pad_all(menuCont, 10, 0);
    
    // Titel erstellen
    titleLabel = lv_label_create(menuCont);
    lv_obj_add_style(titleLabel, &styleMenuTitle, 0);
    lv_label_set_text(titleLabel, title);
    
    // Trennlinie nach Titel
    lv_obj_t* line = lv_line_create(menuCont);
    static lv_point_t line_points[] = {{0, 0}, {width - 30, 0}};
    lv_line_set_points(line, line_points, 2);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(0x00DDDD), 0);
    lv_obj_set_style_margin_top(line, 5, 0);
    lv_obj_set_style_margin_bottom(line, 8, 0);
  }
  
  /**
   * Fügt einen Menüeintrag hinzu.
   * 
   * @param text Der anzuzeigende Text
   * @param callback Die aufzurufende Funktion
   * @return Index des hinzugefügten Menüeintrags
   */
  int addItem(const char* text, MenuCallback callback) {
    MenuItem item;
    item.text = text;
    item.callback = callback;
    item.hasParam = false;
    
    items.push_back(item);
    int index = items.size() - 1;
    
    // Button für Menüeintrag erstellen
    createMenuButton(text, index);
    
    return index;
  }
  
  /**
   * Fügt einen Menüeintrag mit Parameter hinzu.
   * 
   * @param text Der anzuzeigende Text
   * @param callback Die aufzurufende Funktion mit Parameter
   * @param param Der zu übergebende Parameter
   * @return Index des hinzugefügten Menüeintrags
   */
  int addItemWithParam(const char* text, ParamMenuCallback callback, int param) {
    MenuItem item;
    item.text = text;
    item.paramCallback = callback;
    item.param = param;
    item.hasParam = true;
    
    items.push_back(item);
    int index = items.size() - 1;
    
    // Button für Menüeintrag erstellen
    createMenuButton(text, index);
    
    return index;
  }
  
  /**
   * Aktualisiert den Text eines Menüeintrags.
   * 
   * @param index Index des zu aktualisierenden Menüeintrags
   * @param text Der neue Text
   */
  void updateItemText(int index, const char* text) {
    if (index >= 0 && index < items.size()) {
      items[index].text = text;
      
      // Button finden und Text aktualisieren
      lv_obj_t* btn = lv_obj_get_child(menuCont, index + 2); // +2 wegen Titel und Trennlinie
      if (btn) {
        lv_obj_t* label = lv_obj_get_child(btn, 0);
        if (label) {
          lv_label_set_text(label, text);
        }
      }
    }
  }
  
  /**
   * Setzt die Sichtbarkeit des Menüs.
   * 
   * @param visible true für sichtbar, false für unsichtbar
   */
  void setVisible(bool visible) {
    lv_obj_set_style_opa(menuCont, visible ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_click(menuCont, visible);
  }
  
  /**
   * Aktiviert oder deaktiviert einen Menüeintrag.
   * 
   * @param index Index des zu ändernden Menüeintrags
   * @param enabled true für aktiviert, false für deaktiviert
   */
  void setItemEnabled(int index, bool enabled) {
    if (index >= 0 && index < items.size()) {
      // Button finden und aktivieren/deaktivieren
      lv_obj_t* btn = lv_obj_get_child(menuCont, index + 2); // +2 wegen Titel und Trennlinie
      if (btn) {
        lv_obj_set_click(btn, enabled);
        
        if (enabled) {
          lv_obj_clear_state(btn, LV_STATE_DISABLED);
        } else {
          lv_obj_add_state(btn, LV_STATE_DISABLED);
        }
      }
    }
  }
  
private:
  /**
   * Initialisiert die Styles für das Menü.
   */
  void initStyles() {
    // Style für Menüeinträge
    lv_style_init(&styleMenuItem);
    lv_style_set_width(&styleMenuItem, lv_pct(100));
    lv_style_set_height(&styleMenuItem, LV_SIZE_CONTENT);
    lv_style_set_bg_color(&styleMenuItem, lv_color_hex(0x005577));
    lv_style_set_bg_opa(&styleMenuItem, LV_OPA_70);
    lv_style_set_border_width(&styleMenuItem, 0);
    lv_style_set_radius(&styleMenuItem, 10);
    lv_style_set_pad_all(&styleMenuItem, 10);
    lv_style_set_text_color(&styleMenuItem, lv_color_hex(0xFFFFFF));
    
    // Style für ausgewählte Menüeinträge
    lv_style_init(&styleMenuItemSelected);
    lv_style_set_bg_color(&styleMenuItemSelected, lv_color_hex(0x00BBDD));
    lv_style_set_text_color(&styleMenuItemSelected, lv_color_hex(0xFFFFFF));
    
    // Style für den Menü-Container
    lv_style_init(&styleMenuContainer);
    lv_style_set_bg_color(&styleMenuContainer, lv_color_hex(0x003344));
    lv_style_set_bg_opa(&styleMenuContainer, LV_OPA_80);
    lv_style_set_border_width(&styleMenuContainer, 2);
    lv_style_set_border_color(&styleMenuContainer, lv_color_hex(0x006688));
    lv_style_set_radius(&styleMenuContainer, 15);
    lv_style_set_shadow_width(&styleMenuContainer, 10);
    lv_style_set_shadow_opa(&styleMenuContainer, LV_OPA_50);
    
    // Style für den Menütitel
    lv_style_init(&styleMenuTitle);
    lv_style_set_text_font(&styleMenuTitle, &lv_font_montserrat_22);
    lv_style_set_text_color(&styleMenuTitle, lv_color_hex(0x00DDDD));
  }
  
  /**
   * Erstellt einen Button für einen Menüeintrag.
   * 
   * @param text Der anzuzeigende Text
   * @param index Der Index des Menüeintrags
   */
  void createMenuButton(const char* text, int index) {
    lv_obj_t* btn = lv_btn_create(menuCont);
    lv_obj_add_style(btn, &styleMenuItem, 0);
    lv_obj_add_style(btn, &styleMenuItemSelected, LV_STATE_PRESSED);
    
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    
    // Event-Handler für den Button
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
      // Menüsystem-Zeiger und Index aus Event-Daten abrufen
      MenuSystem* menu = (MenuSystem*)lv_event_get_user_data(e);
      uint32_t index = *((uint32_t*)lv_event_get_param(e));
      
      // Callback aufrufen
      if (index < menu->items.size()) {
        if (menu->items[index].hasParam) {
          if (menu->items[index].paramCallback) {
            menu->items[index].paramCallback(menu->items[index].param);
          }
        } else {
          if (menu->items[index].callback) {
            menu->items[index].callback();
          }
        }
      }
    }, LV_EVENT_CLICKED, this);
    
    // Index als Parameter speichern
    static uint32_t indices[50]; // Für bis zu 50 Menüeinträge
    if (index < 50) {
      indices[index] = index;
      lv_obj_set_user_data(btn, &indices[index]);
    }
  }
};

#endif // MENU_H