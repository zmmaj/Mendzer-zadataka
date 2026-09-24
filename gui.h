/*
 * Menadžer Zadataka — GUI specifični tipovi
 * (logika i podaci ostaju u top.h)
 */

 #ifndef TOP_GUI_H_
 #define TOP_GUI_H_
 
 #include <stdbool.h>
 #include <errno.h>
 
 #include <ui/ui.h>
 #include <ui/window.h>
 #include <ui/wdecor.h>
 #include <ui/menu.h> 
 #include <ui/menubar.h>
 #include <ui/menudd.h>
 #include <ui/menuentry.h>
 #include <ui/fixed.h>
 #include <ui/list.h>
 #include <ui/resource.h>
 #include <ui/pbutton.h>
 #include <fibril_synch.h>

 #include <gfx/coord.h>
 #include <gfx/bitmap.h>   /* ako gfx_rect_t nije u coord.h */
 
 #include "top.h"

 
 #define MENU_BAR_Y_OFFSET   20
 #define MENU_BAR_HEIGHT     1
 #define LIST_Y_OFFSET       (MENU_BAR_Y_OFFSET + MENU_BAR_HEIGHT)

 #define STOP_BTN_WIDTH   160   /* širina dugmeta u pikselima */
 #define STOP_BTN_HEIGHT  20    /* visina */
 #define STOP_BTN_MARGIN  4     /* desni razmak od ivice */

 /* Adaptivni timer — po uzoru na animacija.cpp */
#define MIN_INTERVAL_US   (500  * 1000)   /* 0.5 s — najbrže */
#define MAX_INTERVAL_US   (5000 * 1000)   /* 5 s   — najsporije */
#define INIT_INTERVAL_US  (1000 * 1000)   /* 1 s   — start */

#define LOAD_LOW          1
#define LOAD_HIGH         4

 /* Geometrija elemenata unutar fiksnog rasporeda */
 typedef struct {
     gfx_rect_t mbar_rect;
     gfx_rect_t list_rect;
 } guitop_geom_t;
 
 /* Glavna struktura GUI aplikacije */
 typedef struct {
    ui_t *ui;
    ui_window_t *window;
    ui_menu_bar_t *mbar;
    ui_menu_t *mmenu;

    ui_pbutton_t *stop_btn; 

    ui_fixed_t *fixed;
    ui_list_t *list;
    gfx_rect_t list_rect;      /* <-- DODAJ: pamti rect liste */

    fibril_timer_t *timer;
    usec_t interval_us;

    data_t data;
    data_t data_prev;
    bool ima_starih_podataka;
} guitop_t;
 
 int guitop_main(int argc, char *argv[]);
 void top_list_refresh(guitop_t *g);
 void timer_callback(void *arg);
 void format_header(table_t *t, char *buf, size_t bufsz);
 void format_row(table_t *t, size_t row, char *buf, size_t bufsz);
 uint64_t get_selected_task_id(guitop_t *g);
 void restore_selection(guitop_t *g, uint64_t task_id);
 #endif /* TOP_GUI_H_ */
