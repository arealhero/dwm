/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

#ifdef XINERAMA
    #include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */

#include <X11/Xft/Xft.h>
#include <X11/XF86keysym.h> /* XF86XK_AudioLowerVolume */

#include "drw.h"
#include "util.h"

/* macros */
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & current_tags(C->monitor)))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define WIDTH(X)                ((X)->w + 2 * (X)->border_width)
#define HEIGHT(X)               ((X)->h + 2 * (X)->border_width)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union {
    int i;
    unsigned int ui;
    float f;
    const void* v;
} Arg;

typedef struct {
    unsigned int click;
    unsigned int mask;
    unsigned int button;
    void (*func)(const Arg* arg);
    const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
    char name[256];
    float mina, maxa;
    int x, y, w, h;
    int oldx, oldy, oldw, oldh;
    int basew, baseh, incw, inch, maxw, maxh, minw, minh;
    int border_width, old_border_width;
    unsigned int tags;
    int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
    Client* next;
    Client* stack_next;
    Monitor* monitor;
    Window window;
};

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg*);
    const Arg arg;
} Key;

typedef struct {
    const char* symbol;
    void (*arrange)(Monitor*);
} Layout;

typedef struct {
    const char* class;
    const char* instance;
    const char* title;
    unsigned int tags;
    int isfloating;
    int monitor;
} Rule;

/* function declarations */
static void applyrules(Client* c);
static int applysizehints(Client* c, int* x, int* y, int* w, int* h, int interact);
static void arrange(Monitor* m);
static void arrangemon(Monitor* m);
static void attach(Client* c);
static void attachstack(Client* c);
static void buttonpress(XEvent* e);
static void die_if_other_wm_is_running(void);
static void cleanup(void);
static void cleanupmon(Monitor* mon);
static void clientmessage(XEvent* e);
static void configure(Client* c);
static void configurenotify(XEvent* e);
static void configurerequest(XEvent* e);
static Monitor* createmon(void);
static void destroynotify(XEvent* e);
static void detach_client(Client* c);
static void detachstack(Client* c);
static Monitor* dirtomon(int dir);
static void drawbar(Monitor* m);
static void drawbars(void);
static void enternotify(XEvent* e);
static void expose(XEvent* e);
static void focus(Client* c);
static void focusin(XEvent* e);
static void focusmon(const Arg* arg);
static void focusstack(const Arg* arg);
static Atom getatomprop(Client* c, Atom prop);
static int getrootptr(int* x, int* y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char* text, unsigned int size);
static void grabbuttons(Client* c, int focused);
static void grabkeys(void);
static void change_masters_count(const Arg* arg);
static void keypress(XEvent* e);
static void kill_selected_client(const Arg* arg);
static void manage(Window w, XWindowAttributes* wa);
static void mappingnotify(XEvent* e);
static void maprequest(XEvent* e);
static void monocle(Monitor* m);
static void motionnotify(XEvent* e);
static void movemouse(const Arg* arg);
static Client* nexttiled(Client* c);
static void pop(Client*);
static void propertynotify(XEvent* e);
static void quit(const Arg* arg);
static Monitor* recttomon(int x, int y, int w, int h);
static void resize(Client* c, int x, int y, int w, int h, int interact);
static void resizeclient(Client* c, int x, int y, int w, int h);
static void resizemouse(const Arg* arg);
static void restack(Monitor* m);
static void run(void);
static void scan(void);
static int sendevent(Client* c, Atom proto);
static void sendmon(Client* c, Monitor* m);
static void setclientstate(Client* c, long state);
static void setfocus(Client* c);
static void setfullscreen(Client* c, int fullscreen);
static void setgaps(const Arg* arg);
static void setlayout(const Arg* arg);
static void setmfact(const Arg* arg);
static void setup(void);
static void seturgent(Client* c, int urg);
static void showhide(Client* c);
static void sigchld(int unused);
static void spawn(const Arg* arg);
static void tag(const Arg* arg);
static void tagmon(const Arg* arg);
static void tile(Monitor*);
static void togglebar(const Arg* arg);
static void togglefloating(const Arg* arg);
static void toggletag(const Arg* arg);
static void toggleview(const Arg* arg);
static void unfocus(Client* c, int setfocus);
static void unmanage(Client* c, int destroyed);
static void unmapnotify(XEvent* e);
static void updatebarpos(Monitor* m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client* c);
static void updatestatus(void);
static void updatetitle(Client* c);
static void updatewindowtype(Client* c);
static void updatewmhints(Client* c);
static void view(const Arg* arg);
static Client* window_to_client(Window w);
static Monitor* window_to_monitor(Window w);
static int xerror(Display* display, XErrorEvent* ee);
static int xerrordummy(Display* display, XErrorEvent* ee);
static int xerrorstart(Display* display, XErrorEvent* ee);
static void zoom(const Arg* arg);

/* variables */
static const char broken[] = "broken";
static char status_text[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh, blw = 0;      /* bar geometry */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display*, XErrorEvent*);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent*) = {
    [ButtonPress]      = buttonpress,
    [ClientMessage]    = clientmessage,
    [ConfigureRequest] = configurerequest,
    [ConfigureNotify]  = configurenotify,
    [DestroyNotify]    = destroynotify,
    [EnterNotify]      = enternotify,
    [Expose]           = expose,
    [FocusIn]          = focusin,
    [KeyPress]         = keypress,
    [MappingNotify]    = mappingnotify,
    [MapRequest]       = maprequest,
    [MotionNotify]     = motionnotify,
    [PropertyNotify]   = propertynotify,
    [UnmapNotify]      = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static int running = 1;
static Cur* cursor[CurLast];
static Clr** scheme;
static Display* dpy;
static Drw* drw;
static Monitor* monitors, *selected_monitor;
static Window root_window, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

struct Monitor {
    char layout_symbol[8];
    float mfact;
    unsigned int masters_count;
    int num;
    int by;               /* bar geometry */
    int mx, my, mw, mh;   /* screen size */
    int wx, wy, ww, wh;   /* window area  */
    int gappx;            /* gaps between windows */
    unsigned int selected_tags_set;
    unsigned int tagset[2];
    int showbar;
    int topbar;
    Client* clients;
    Client* selected_client;
    Client* stack;
    Monitor* next;
    Window bar_window;

    unsigned int current_layout_index;
    const Layout* layouts[LENGTH(tags)];
};

/* function implementations */

/* tags */
static inline unsigned int
current_tags(Monitor* monitor)
{
    return monitor->tagset[monitor->selected_tags_set];
}

static inline void
set_tags(Monitor* monitor, const unsigned int tag)
{
    monitor->tagset[monitor->selected_tags_set] = tag;
}

static inline void
swap_selected_tags(Monitor* monitor)
{
    monitor->selected_tags_set ^= 1;
}

/* layouts */
static inline const Layout*
current_layout(Monitor* monitor)
{
    return monitor->layouts[monitor->current_layout_index];
}

static inline void
set_layout_index(Monitor* monitor, const unsigned int index)
{
    monitor->current_layout_index = index;
}

static inline void
set_layout(Monitor* monitor, const Layout* layout)
{
    monitor->layouts[monitor->current_layout_index] = layout;
}

static inline void
copy_layout_symbol(Monitor* monitor)
{
    const Layout* layout = current_layout(monitor);
    strncpy(monitor->layout_symbol, layout->symbol, sizeof monitor->layout_symbol);
}

/* Tiling */
static inline unsigned int
count_tiled_clients(Monitor* monitor)
{
    unsigned int count = 0;
    Client* client = nexttiled(monitor->clients);

    while (client != NULL) {
        ++count;
        client = nexttiled(client->next);
    }

    return count;
}

// NOTE: returns the last monitor if `current_monitor' is NULL.
static inline Monitor*
find_previous_monitor(Monitor* current_monitor) {
    Monitor* monitor = monitors;

    while (monitor && monitor->next != current_monitor) {
        monitor = monitor->next;
    }

    return monitor;
}

static inline Client*
find_first_visible_client_in_stack(Client* start) {
    while (start && !ISVISIBLE(start)) {
        start = start->stack_next;
    }

    return start;
}

void
applyrules(Client* c)
{
    /* rule matching */
    c->isfloating = 0;
    c->tags = 0;

    XClassHint hint = { NULL, NULL };
    XGetClassHint(dpy, c->window, &hint);

    const char* class    = hint.res_class ? hint.res_class : broken;
    const char* instance = hint.res_name  ? hint.res_name  : broken;

    for (size_t i = 0; i < LENGTH(rules); i++) {
        const Rule* rule = &rules[i];

        if ((!rule->title || strstr(c->name, rule->title))
            && (!rule->class || strstr(class, rule->class))
            && (!rule->instance || strstr(instance, rule->instance)))
        {
            c->isfloating = rule->isfloating;
            c->tags |= rule->tags;

            Monitor* m = monitors;
            while (m && m->num != rule->monitor) {
                m = m->next;
            }

            if (m) {
                c->monitor = m;
            }
        }
    }

    if (hint.res_class) {
        XFree(hint.res_class);
    }

    if (hint.res_name) {
        XFree(hint.res_name);
    }

    c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : current_tags(c->monitor);
}

int
applysizehints(Client* c, int* x, int* y, int* w, int* h, int interact)
{
    int baseismin;
    Monitor* m = c->monitor;

    /* set minimum possible */
    *w = MAX(1, *w);
    *h = MAX(1, *h);
    if (interact) {
        if (*x > sw) {
            *x = sw - WIDTH(c);
        }

        if (*y > sh) {
            *y = sh - HEIGHT(c);
        }

        if (*x + *w + 2 * c->border_width < 0) {
            *x = 0;
        }

        if (*y + *h + 2 * c->border_width < 0) {
            *y = 0;
        }
    } else {
        if (*x >= m->wx + m->ww) {
            *x = m->wx + m->ww - WIDTH(c);
        }

        if (*y >= m->wy + m->wh) {
            *y = m->wy + m->wh - HEIGHT(c);
        }

        if (*x + *w + 2 * c->border_width <= m->wx) {
            *x = m->wx;
        }

        if (*y + *h + 2 * c->border_width <= m->wy) {
            *y = m->wy;
        }
    }

    if (*h < bh) {
        *h = bh;
    }

    if (*w < bh) {
        *w = bh;
    }

    if (resizehints || c->isfloating || !current_layout(c->monitor)->arrange) {
        /* see last two sentences in ICCCM 4.1.2.3 */
        baseismin = c->basew == c->minw && c->baseh == c->minh;
        if (!baseismin) { /* temporarily remove base dimensions */
            *w -= c->basew;
            *h -= c->baseh;
        }

        /* adjust for aspect limits */
        if (c->mina > 0 && c->maxa > 0) {
            if (c->maxa < (float)*w / *h) {
                *w = *h * c->maxa + 0.5;
            } else if (c->mina < (float)*h / *w) {
                *h = *w * c->mina + 0.5;
            }
        }

        /* increment calculation requires this */
        if (baseismin) {
            *w -= c->basew;
            *h -= c->baseh;
        }

        /* adjust for increment value */
        if (c->incw) {
            *w -= *w % c->incw;
        }

        if (c->inch) {
            *h -= *h % c->inch;
        }

        /* restore base dimensions */
        *w = MAX(*w + c->basew, c->minw);
        *h = MAX(*h + c->baseh, c->minh);

        if (c->maxw) {
            *w = MIN(*w, c->maxw);
        }

        if (c->maxh) {
            *h = MIN(*h, c->maxh);
        }
    }

    return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor* monitor)
{
    if (monitor) {
        showhide(monitor->stack);
        arrangemon(monitor);
        restack(monitor);
        return;
    }

    monitor = monitors;
    while (monitor != NULL) {
        showhide(monitor->stack);
        arrangemon(monitor);

        monitor = monitor->next;
    }
}

void
arrangemon(Monitor* monitor)
{
    copy_layout_symbol(monitor);

    const Layout* layout = current_layout(monitor);
    if (layout->arrange) {
        layout->arrange(monitor);
    }
}

void
attach(Client* c)
{
    c->next = c->monitor->clients;
    c->monitor->clients = c;
}

void
attachstack(Client* c)
{
    c->stack_next = c->monitor->stack;
    c->monitor->stack = c;
}

void
buttonpress(XEvent* e)
{
    unsigned int i, click;
    Arg arg = {0};
    Client* c;
    Monitor* m;
    XButtonPressedEvent* ev = &e->xbutton;

    click = ClkRootWin;
    /* focus monitor if necessary */
    if ((m = window_to_monitor(ev->window)) && m != selected_monitor) {
        unfocus(selected_monitor->selected_client, 1);
        selected_monitor = m;
        focus(NULL);
    }

    if (ev->window == selected_monitor->bar_window) {
        i = 0;

        int x = 0;
        do {
            x += TEXTW(tags[i]);
        } while (ev->x >= x && ++i < LENGTH(tags));

        if (i < LENGTH(tags)) {
            click = ClkTagBar;
            arg.ui = 1 << i;
        } else if (ev->x < x + blw) {
            click = ClkLtSymbol;
        } else if (ev->x > selected_monitor->ww - (int)TEXTW(status_text)) {
            click = ClkStatusText;
        } else {
            click = ClkWinTitle;
        }
    } else if ((c = window_to_client(ev->window))) {
        focus(c);
        restack(selected_monitor);
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        click = ClkClientWin;
    }

    for (i = 0; i < LENGTH(buttons); i++) {
        if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
            && CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
        {
            buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
        }
    }
}

void
die_if_other_wm_is_running(void)
{
    xerrorxlib = XSetErrorHandler(xerrorstart);
    /* this causes an error if some other window manager is running */
    XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XSync(dpy, False);
}

void
cleanup(void)
{
    Arg a = {.ui = ~0};
    view(&a);

    Layout dummy_layout = { "", NULL };
    set_layout(selected_monitor, &dummy_layout);

    for (Monitor* m = monitors; m != NULL; m = m->next) {
        while (m->stack) {
            unmanage(m->stack, 0);
        }
    }

    XUngrabKey(dpy, AnyKey, AnyModifier, root_window);

    while (monitors) {
        cleanupmon(monitors);
    }

    for (size_t i = 0; i < CurLast; i++) {
        drw_cur_free(drw, cursor[i]);
    }

    for (size_t i = 0; i < LENGTH(colors); i++) {
        free(scheme[i]);
    }

    XDestroyWindow(dpy, wmcheckwin);
    drw_free(drw);
    XSync(dpy, False);
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root_window, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor* monitor)
{
    if (monitor == monitors) {
        monitors = monitors->next;
    } else {
        Monitor* previous = find_previous_monitor(monitor);

        if (previous) {
            previous->next = monitor->next;
        }
    }

    XUnmapWindow(dpy, monitor->bar_window);
    XDestroyWindow(dpy, monitor->bar_window);
    free(monitor);
}

void
clientmessage(XEvent* e)
{
    XClientMessageEvent* cme = &e->xclient;
    Client* client = window_to_client(cme->window);

    if (!client) {
        return;
    }

    if (cme->message_type == netatom[NetWMState]) {
        if ((unsigned long)cme->data.l[1] == netatom[NetWMFullscreen]
            || (unsigned long)cme->data.l[2] == netatom[NetWMFullscreen])
        {
            setfullscreen(client, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !client->isfullscreen)));
        }
    } else if (cme->message_type == netatom[NetActiveWindow]) {
        if (client != selected_monitor->selected_client && !client->isurgent) {
            seturgent(client, 1);
        }
    }
}

void
configure(Client* c)
{
    XConfigureEvent ce;

    ce.type = ConfigureNotify;
    ce.display = dpy;
    ce.event = c->window;
    ce.window = c->window;
    ce.x = c->x;
    ce.y = c->y;
    ce.width = c->w;
    ce.height = c->h;
    ce.border_width = c->border_width;
    ce.above = None;
    ce.override_redirect = False;
    XSendEvent(dpy, c->window, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent* e)
{
    XConfigureEvent* ev = &e->xconfigure;

    /* TODO: updategeom handling sucks, needs to be simplified */
    if (ev->window == root_window) {
        sw = ev->width;
        sh = ev->height;

        const int dirty = (sw != ev->width || sh != ev->height);
        if (updategeom() || dirty) {
            drw_resize(drw, sw, bh);
            updatebars();

            for (Monitor* m = monitors; m != NULL; m = m->next) {
                for (Client* c = m->clients; c != NULL; c = c->next) {
                    if (c->isfullscreen) {
                        resizeclient(c, m->mx, m->my, m->mw, m->mh);
                    }
                }

                XMoveResizeWindow(dpy, m->bar_window, m->wx, m->by, m->ww, bh);
            }

            focus(NULL);
            arrange(NULL);
        }
    }
}

void
configurerequest(XEvent* e)
{
    Client* c;
    Monitor* m;
    XConfigureRequestEvent* ev = &e->xconfigurerequest;
    XWindowChanges wc;

    if ((c = window_to_client(ev->window))) {
        if (ev->value_mask & CWBorderWidth) {
            c->border_width = ev->border_width;
        } else if (c->isfloating || !current_layout(selected_monitor)->arrange) {
            m = c->monitor;

            if (ev->value_mask & CWX) {
                c->oldx = c->x;
                c->x = m->mx + ev->x;
            }

            if (ev->value_mask & CWY) {
                c->oldy = c->y;
                c->y = m->my + ev->y;
            }

            if (ev->value_mask & CWWidth) {
                c->oldw = c->w;
                c->w = ev->width;
            }

            if (ev->value_mask & CWHeight) {
                c->oldh = c->h;
                c->h = ev->height;
            }

            if ((c->x + c->w) > m->mx + m->mw && c->isfloating) {
                c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
            }

            if ((c->y + c->h) > m->my + m->mh && c->isfloating) {
                c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
            }

            if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight))) {
                configure(c);
            }

            if (ISVISIBLE(c)) {
                XMoveResizeWindow(dpy, c->window, c->x, c->y, c->w, c->h);
            }
        } else {
            configure(c);
        }
    } else {
        wc.x = ev->x;
        wc.y = ev->y;
        wc.width = ev->width;
        wc.height = ev->height;
        wc.border_width = ev->border_width;
        wc.sibling = ev->above;
        wc.stack_mode = ev->detail;
        XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
    }

    XSync(dpy, False);
}

Monitor*
createmon(void)
{
    Monitor* m = ecalloc(1, sizeof(Monitor));

    m->tagset[0] = m->tagset[1] = 1;
    m->mfact = mfact;
    m->masters_count = DEFAULT_MASTERS_COUNT;
    m->showbar = showbar;
    m->topbar = topbar;
    m->gappx = gappx;

    for (unsigned int i = 0; i < LENGTH(tags); ++i) {
        m->layouts[i] = &layouts[0];
    }

    copy_layout_symbol(m);

    return m;
}

void
destroynotify(XEvent* e)
{
    XDestroyWindowEvent* ev = &e->xdestroywindow;

    Client* c;
    if ((c = window_to_client(ev->window))) {
        unmanage(c, 1);
    }
}

void
detach_client(Client* client)
{
    Client** previous_client = &client->monitor->clients;

    while (*previous_client && *previous_client != client) {
        previous_client = &((*previous_client)->next);
    }

    *previous_client = client->next;
}

void
detachstack(Client* client)
{
    Monitor* monitor = client->monitor;

    Client** previous_client = &monitor->stack;

    while (*previous_client && *previous_client != client) {
        previous_client = &((*previous_client)->stack_next);
    }

    *previous_client = client->stack_next;

    // Find and select other client if the detached one is currently selected.
    if (client == monitor->selected_client) {
        monitor->selected_client = find_first_visible_client_in_stack(monitor->stack);
    }
}

Monitor*
dirtomon(int dir)
{
    Monitor* monitor = NULL;

    if (dir > 0) {
        if (!(monitor = selected_monitor->next)) {
            monitor = monitors;
        }
    } else if (selected_monitor == monitors) {
        monitor = find_previous_monitor(NULL);
    } else {
        monitor = find_previous_monitor(selected_monitor);
    }

    return monitor;
}

void
drawbar(Monitor* m)
{
    int x, w, tw = 0;
    int boxs = drw->fonts->h / 9;
    int boxw = drw->fonts->h / 6 + 2;
    unsigned int occ = 0, urg = 0;

    /* draw status first so it can be overdrawn by tags later */
    if (m == selected_monitor) { /* status is only drawn on selected monitor */
        drw_setscheme(drw, scheme[SchemeNorm]);
        tw = TEXTW(status_text) - lrpad + 2; /* 2px right padding */
        drw_text(drw, m->ww - tw, 0, tw, bh, 0, status_text, 0);
    }

    for (Client* c = m->clients; c != NULL; c = c->next) {
        occ |= c->tags;
        if (c->isurgent) {
            urg |= c->tags;
        }
    }

    x = 0;
    for (unsigned int i = 0; i < LENGTH(tags); i++) {
        w = TEXTW(tags[i]);
        drw_setscheme(drw, scheme[current_tags(m) & 1 << i ? SchemeSel : SchemeNorm]);
        drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);
        if (occ & 1 << i) {
            const int filled = (m == selected_monitor)
                               && selected_monitor->selected_client
                               && (selected_monitor->selected_client->tags & 1 << i);

            drw_rect(drw, x + boxs, boxs, boxw, boxw, filled, urg & 1 << i);
        }
        x += w;
    }

    w = blw = TEXTW(m->layout_symbol);
    drw_setscheme(drw, scheme[SchemeNorm]);
    x = drw_text(drw, x, 0, w, bh, lrpad / 2, m->layout_symbol, 0);

    if ((w = m->ww - tw - x) > bh) {
        if (m->selected_client) {
            drw_setscheme(drw, scheme[m == selected_monitor ? SchemeSel : SchemeNorm]);
            drw_text(drw, x, 0, w, bh, lrpad / 2, m->selected_client->name, 0);
            if (m->selected_client->isfloating) {
                drw_rect(drw, x + boxs, boxs, boxw, boxw, m->selected_client->isfixed, 0);
            }
        } else {
            drw_setscheme(drw, scheme[SchemeNorm]);
            drw_rect(drw, x, 0, w, bh, 1, 1);
        }
    }

    drw_map(drw, m->bar_window, 0, 0, m->ww, bh);
}

void
drawbars(void)
{
    for (Monitor* m = monitors; m != NULL; m = m->next) {
        drawbar(m);
    }
}

void
enternotify(XEvent* e)
{
    Client* c;
    Monitor* m;
    XCrossingEvent* ev = &e->xcrossing;

    if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root_window) {
        return;
    }

    c = window_to_client(ev->window);
    m = c ? c->monitor : window_to_monitor(ev->window);
    if (m != selected_monitor) {
        unfocus(selected_monitor->selected_client, 1);
        selected_monitor = m;
    } else if (!c || c == selected_monitor->selected_client) {
        return;
    }

    focus(c);
}

void
expose(XEvent* e)
{
    Monitor* m;
    XExposeEvent* ev = &e->xexpose;

    if (ev->count == 0 && (m = window_to_monitor(ev->window))) {
        drawbar(m);
    }
}

void
focus(Client* c)
{
    if (!c || !ISVISIBLE(c)) {
        c = find_first_visible_client_in_stack(selected_monitor->stack);
    }

    if (selected_monitor->selected_client && selected_monitor->selected_client != c) {
        unfocus(selected_monitor->selected_client, 0);
    }

    if (c) {
        if (c->monitor != selected_monitor) {
            selected_monitor = c->monitor;
        }

        if (c->isurgent) {
            seturgent(c, 0);
        }

        detachstack(c);
        attachstack(c);
        grabbuttons(c, 1);
        XSetWindowBorder(dpy, c->window, scheme[SchemeSel][ColBorder].pixel);
        setfocus(c);
    } else {
        XSetInputFocus(dpy, root_window, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root_window, netatom[NetActiveWindow]);
    }

    selected_monitor->selected_client = c;
    drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent* e)
{
    XFocusChangeEvent* ev = &e->xfocus;

    if (selected_monitor->selected_client && ev->window != selected_monitor->selected_client->window) {
        setfocus(selected_monitor->selected_client);
    }
}

void
focusmon(const Arg* arg)
{
    if (!monitors->next) {
        return;
    }

    Monitor* m;
    if ((m = dirtomon(arg->i)) == selected_monitor) {
        return;
    }

    unfocus(selected_monitor->selected_client, 0);
    selected_monitor = m;
    focus(NULL);
}

static inline Client*
find_first_visible_client(Client* start, Client* end) {
    while (start != end && !ISVISIBLE(start)) {
        start = start->next;
    }

    return start;
}

static inline Client*
find_last_visible_client(Client* start, Client* end) {
    Client* last_visible_client = NULL;

    while (start != end) {
        if (ISVISIBLE(start)) {
            last_visible_client = start;
        }

        start = start->next;
    }

    return last_visible_client;
}

void
focusstack(const Arg* arg)
{
    Client* selected_client = selected_monitor->selected_client;
    if (!selected_client || (selected_client->isfullscreen && lockfullscreen)) {
        return;
    }

    Client* c = NULL;

    if (arg->i > 0) {
        c = find_first_visible_client(selected_client->next, NULL);

        if (!c) {
            c = find_first_visible_client(selected_monitor->clients, selected_client);
        }
    } else {
        c = find_last_visible_client(selected_monitor->clients, selected_client);

        if (!c) {
            c = find_last_visible_client(selected_client->next, NULL);
        }
    }

    if (c) {
        focus(c);
        restack(selected_monitor);
    }
}

Atom
getatomprop(Client* c, Atom prop)
{
    int di;
    unsigned long dl;
    unsigned char* p = NULL;
    Atom da, atom = None;

    if (XGetWindowProperty(dpy, c->window, prop, 0L, sizeof atom, False, XA_ATOM,
                           &da, &di, &dl, &dl, &p) == Success && p)
    {
        atom = *(Atom*)p;
        XFree(p);
    }
    return atom;
}

int
getrootptr(int* x, int* y)
{
    int di;
    unsigned int dui;
    Window dummy;

    return XQueryPointer(dpy, root_window, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
    int format;
    long result = -1;
    unsigned char* p = NULL;
    unsigned long n, extra;
    Atom real;

    const int status = XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
                                           &real, &format, &n, &extra, (unsigned char**)&p);
    if (status != Success) {
        return -1;
    }

    if (n != 0) {
        result = *p;
    }

    XFree(p);
    return result;
}

int
gettextprop(Window w, Atom atom, char* text, unsigned int size)
{
    if (!text || size == 0) {
        return 0;
    }

    text[0] = '\0';

    XTextProperty name;
    if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems) {
        return 0;
    }

    if (name.encoding == XA_STRING) {
        strncpy(text, (char*)name.value, size - 1);
    } else {
        char** list = NULL;
        int n;

        if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
            strncpy(text, *list, size - 1);
            XFreeStringList(list);
        }
    }

    text[size - 1] = '\0';
    XFree(name.value);
    return 1;
}

void
grabbuttons(Client* c, int focused)
{
    const unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };

    updatenumlockmask();

    XUngrabButton(dpy, AnyButton, AnyModifier, c->window);
    if (!focused) {
        XGrabButton(dpy, AnyButton, AnyModifier, c->window, False,
            BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
    }

    for (unsigned int i = 0; i < LENGTH(buttons); i++) {
        if (buttons[i].click != ClkClientWin) {
            continue;
        }

        for (unsigned int j = 0; j < LENGTH(modifiers); j++) {
            XGrabButton(dpy, buttons[i].button,
                buttons[i].mask | modifiers[j],
                c->window, False, BUTTONMASK,
                GrabModeAsync, GrabModeSync, None, None);
        }
    }
}

void
grabkeys(void)
{
    const unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };

    updatenumlockmask();

    XUngrabKey(dpy, AnyKey, AnyModifier, root_window);
    for (unsigned int i = 0; i < LENGTH(keys); i++) {
        const KeyCode code = XKeysymToKeycode(dpy, keys[i].keysym);

        if (!code) {
            continue;
        }

        for (unsigned int j = 0; j < LENGTH(modifiers); j++) {
            XGrabKey(dpy, code, keys[i].mod | modifiers[j], root_window,
                True, GrabModeAsync, GrabModeAsync);
        }
    }
}

void
change_masters_count(const Arg* arg)
{
    selected_monitor->masters_count = MAX(selected_monitor->masters_count + arg->i, 1);
    arrange(selected_monitor);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo* unique, size_t n, XineramaScreenInfo* info)
{
    while (n--) {
        if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
            && unique[n].width == info->width && unique[n].height == info->height)
        {
            return 0;
        }
    }

    return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent* e)
{
    const XKeyEvent* ev = &e->xkey;
    const KeySym keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);

    for (unsigned int i = 0; i < LENGTH(keys); i++) {
        if (keysym == keys[i].keysym
            && CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
            && keys[i].func)
        {
            keys[i].func(&(keys[i].arg));
        }
    }
}

void
kill_selected_client(const Arg* arg)
{
    (void) arg;

    if (!selected_monitor->selected_client) {
        return;
    }

    if (!sendevent(selected_monitor->selected_client, wmatom[WMDelete])) {
        XGrabServer(dpy);
        XSetErrorHandler(xerrordummy);
        XSetCloseDownMode(dpy, DestroyAll);
        XKillClient(dpy, selected_monitor->selected_client->window);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }
}

void
manage(Window w, XWindowAttributes* wa)
{
    Client* c, *t = NULL;
    Window trans = None;
    XWindowChanges wc;

    c = ecalloc(1, sizeof(Client));
    c->window = w;
    /* geometry */
    c->x = c->oldx = wa->x;
    c->y = c->oldy = wa->y;
    c->w = c->oldw = wa->width;
    c->h = c->oldh = wa->height;
    c->old_border_width = wa->border_width;

    updatetitle(c);
    if (XGetTransientForHint(dpy, w, &trans) && (t = window_to_client(trans))) {
        c->monitor = t->monitor;
        c->tags = t->tags;
    } else {
        c->monitor = selected_monitor;
        applyrules(c);
    }

    Monitor* monitor = c->monitor;

    if (c->x + WIDTH(c) > monitor->mx + monitor->mw) {
        c->x = monitor->mx + monitor->mw - WIDTH(c);
    }

    if (c->y + HEIGHT(c) > monitor->my + monitor->mh) {
        c->y = monitor->my + monitor->mh - HEIGHT(c);
    }

    c->x = MAX(c->x, monitor->mx);
    /* only fix client y-offset, if the client center might cover the bar */
    c->y = MAX(c->y, ((monitor->by == monitor->my)
                      && (c->x + (c->w / 2) >= monitor->wx)
                      && (c->x + (c->w / 2) < monitor->wx + monitor->ww))
                     ? bh
                     : monitor->my);
    c->border_width = borderpx;

    wc.border_width = c->border_width;
    XConfigureWindow(dpy, w, CWBorderWidth, &wc);
    XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
    configure(c); /* propagates border_width, if size doesn't change */
    updatewindowtype(c);
    updatesizehints(c);
    updatewmhints(c);
    c->x = monitor->mx + (monitor->mw - WIDTH(c)) / 2;
    c->y = monitor->my + (monitor->mh - HEIGHT(c)) / 2;
    XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
    grabbuttons(c, 0);

    if (!c->isfloating) {
        c->isfloating = c->oldstate = trans != None || c->isfixed;
    }
    if (c->isfloating) {
        XRaiseWindow(dpy, c->window);
    }

    attach(c);
    attachstack(c);
    XChangeProperty(dpy, root_window, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
        (unsigned char*) &(c->window), 1);
    XMoveResizeWindow(dpy, c->window, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
    setclientstate(c, NormalState);

    if (monitor == selected_monitor) {
        unfocus(selected_monitor->selected_client, 0);
    }
    monitor->selected_client = c;

    arrange(monitor);
    XMapWindow(dpy, c->window);
    focus(NULL);
}

void
mappingnotify(XEvent* e)
{
    XMappingEvent* ev = &e->xmapping;

    XRefreshKeyboardMapping(ev);
    if (ev->request == MappingKeyboard) {
        grabkeys();
    }
}

void
maprequest(XEvent* e)
{
    static XWindowAttributes wa;
    XMapRequestEvent* ev = &e->xmaprequest;

    if (!XGetWindowAttributes(dpy, ev->window, &wa)) {
        return;
    }

    if (wa.override_redirect) {
        return;
    }

    if (!window_to_client(ev->window)) {
        manage(ev->window, &wa);
    }
}

void
monocle(Monitor* m)
{
    // FIXME(vlad): do not draw window borders in the monocle mode.
    //              Or maybe we should draw gaps even in monocle mode? Idk.
    for (Client* c = nexttiled(m->clients); c != NULL; c = nexttiled(c->next)) {
        resize(c, m->wx, m->wy, m->ww - 2 * c->border_width, m->wh - 2 * c->border_width, 0);
    }
}

void
motionnotify(XEvent* e)
{
    static Monitor* mon = NULL;
    XMotionEvent* ev = &e->xmotion;

    if (ev->window != root_window) {
        return;
    }

    Monitor* m;
    if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
        unfocus(selected_monitor->selected_client, 1);
        selected_monitor = m;
        focus(NULL);
    }

    mon = m;
}

void
movemouse(const Arg* arg)
{
    (void) arg;

    Client* c;
    if (!(c = selected_monitor->selected_client)) {
        return;
    }

    if (c->isfullscreen) { /* no support moving fullscreen windows by mouse */
        return;
    }

    restack(selected_monitor);

    const int ocx = c->x;
    const int ocy = c->y;

    if (XGrabPointer(dpy, root_window, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                     None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
    {
        return;
    }

    int x, y;
    if (!getrootptr(&x, &y)) {
        return;
    }

    XEvent ev;
    Time lasttime = 0;
    do {
        XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
        switch (ev.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest: {
            handler[ev.type](&ev);
            break;
        }

        case MotionNotify:
            // FIXME(vlad): remove magic number
            if ((ev.xmotion.time - lasttime) <= (1000 / 150)) {
                continue;
            }

            lasttime = ev.xmotion.time;

            int nx = ocx + (ev.xmotion.x - x);
            int ny = ocy + (ev.xmotion.y - y);
            if (abs(selected_monitor->wx - nx) < snap) {
                nx = selected_monitor->wx;
            } else if (abs((selected_monitor->wx + selected_monitor->ww) - (nx + WIDTH(c))) < snap) {
                nx = selected_monitor->wx + selected_monitor->ww - WIDTH(c);
            }

            if (abs(selected_monitor->wy - ny) < snap) {
                ny = selected_monitor->wy;
            } else if (abs((selected_monitor->wy + selected_monitor->wh) - (ny + HEIGHT(c))) < snap) {
                ny = selected_monitor->wy + selected_monitor->wh - HEIGHT(c);
            }

            const Layout* layout = current_layout(selected_monitor);
            if (!c->isfloating && layout->arrange
                && (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
            {
                togglefloating(NULL);
            }

            if (!layout->arrange || c->isfloating) {
                resize(c, nx, ny, c->w, c->h, 1);
            }

            break;
        }
    } while (ev.type != ButtonRelease);

    XUngrabPointer(dpy, CurrentTime);

    Monitor* m;
    if ((m = recttomon(c->x, c->y, c->w, c->h)) != selected_monitor) {
        sendmon(c, m);
        selected_monitor = m;
        focus(NULL);
    }
}

Client*
nexttiled(Client* c)
{
    while (c != NULL && (c->isfloating || !ISVISIBLE(c))) {
        c = c->next;
    }

    return c;
}

void
pop(Client* c)
{
    detach_client(c);
    attach(c);
    focus(c);
    arrange(c->monitor);
}

void
propertynotify(XEvent* e)
{
    Client* c;
    Window trans;
    XPropertyEvent* ev = &e->xproperty;

    if ((ev->window == root_window) && (ev->atom == XA_WM_NAME)) {
        updatestatus();
    } else if (ev->state == PropertyDelete) {
        return; /* ignore */
    } else if ((c = window_to_client(ev->window))) {
        switch (ev->atom) {
            case XA_WM_TRANSIENT_FOR: {
                if (!c->isfloating && (XGetTransientForHint(dpy, c->window, &trans)) &&
                    (c->isfloating = (window_to_client(trans)) != NULL))
                {
                    arrange(c->monitor);
                }
                break;
            }

            case XA_WM_NORMAL_HINTS: {
                updatesizehints(c);
                break;
            }

            case XA_WM_HINTS: {
                updatewmhints(c);
                drawbars();
                break;
            }

            default: break;
        }

        if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
            updatetitle(c);
            if (c == c->monitor->selected_client) {
                drawbar(c->monitor);
            }
        }

        if (ev->atom == netatom[NetWMWindowType]) {
            updatewindowtype(c);
        }
    }
}

void
quit(const Arg* arg)
{
    (void) arg;

    running = 0;
}

Monitor*
recttomon(int x, int y, int w, int h)
{
    Monitor* r = selected_monitor;
    int area = 0;

    for (Monitor* m = monitors; m != NULL; m = m->next) {
        int a;
        if ((a = INTERSECT(x, y, w, h, m)) > area) {
            area = a;
            r = m;
        }
    }

    return r;
}

void
resize(Client* c, int x, int y, int w, int h, int interact)
{
    if (applysizehints(c, &x, &y, &w, &h, interact)) {
        resizeclient(c, x, y, w, h);
    }
}

void
resizeclient(Client* c, int x, int y, int w, int h)
{
    XWindowChanges wc;

    c->oldx = c->x; c->x = wc.x = x;
    c->oldy = c->y; c->y = wc.y = y;
    c->oldw = c->w; c->w = wc.width = w;
    c->oldh = c->h; c->h = wc.height = h;
    wc.border_width = c->border_width;
    XConfigureWindow(dpy, c->window, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
    configure(c);
    XSync(dpy, False);
}

void
resizemouse(const Arg* arg)
{
    (void) arg;

    int ocx, ocy, nw, nh;
    Client* c;
    XEvent ev;
    Time lasttime = 0;

    if (!(c = selected_monitor->selected_client)) {
        return;
    }

    if (c->isfullscreen) {/* no support resizing fullscreen windows by mouse */
        return;
    }

    restack(selected_monitor);
    ocx = c->x;
    ocy = c->y;

    if (XGrabPointer(dpy, root_window, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
                     None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
    {
        return;
    }

    XWarpPointer(dpy, None, c->window, 0, 0, 0, 0, c->w + c->border_width - 1, c->h + c->border_width - 1);
    do {
        XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
        switch (ev.type) {
        case ConfigureRequest:
        case Expose:
        case MapRequest:
            handler[ev.type](&ev);
            break;
        case MotionNotify:
            if ((ev.xmotion.time - lasttime) <= (1000 / 150))
                continue;
            lasttime = ev.xmotion.time;

            nw = MAX(ev.xmotion.x - ocx - 2 * c->border_width + 1, 1);
            nh = MAX(ev.xmotion.y - ocy - 2 * c->border_width + 1, 1);

            const Layout* layout = current_layout(selected_monitor);
            if (c->monitor->wx + nw >= selected_monitor->wx && c->monitor->wx + nw <= selected_monitor->wx + selected_monitor->ww
             && c->monitor->wy + nh >= selected_monitor->wy && c->monitor->wy + nh <= selected_monitor->wy + selected_monitor->wh)
            {
                if (!c->isfloating && layout->arrange
                    && (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
                {
                    togglefloating(NULL);
                }
            }

            if (!layout->arrange || c->isfloating) {
                resize(c, c->x, c->y, nw, nh, 1);
            }

            break;
        }
    } while (ev.type != ButtonRelease);

    XWarpPointer(dpy, None, c->window, 0, 0, 0, 0, c->w + c->border_width - 1, c->h + c->border_width - 1);
    XUngrabPointer(dpy, CurrentTime);

    while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
        ;

    Monitor* m;
    if ((m = recttomon(c->x, c->y, c->w, c->h)) != selected_monitor) {
        sendmon(c, m);
        selected_monitor = m;
        focus(NULL);
    }
}

void
restack(Monitor* m)
{
    drawbar(m);
    if (!m->selected_client) {
        return;
    }

    const Layout* layout = current_layout(m);
    if (m->selected_client->isfloating || !layout->arrange) {
        XRaiseWindow(dpy, m->selected_client->window);
    }

    if (layout->arrange) {
        XWindowChanges wc;
        wc.stack_mode = Below;
        wc.sibling = m->bar_window;

        for (Client* c = m->stack; c != NULL; c = c->stack_next) {
            if (!c->isfloating && ISVISIBLE(c)) {
                XConfigureWindow(dpy, c->window, CWSibling|CWStackMode, &wc);
                wc.sibling = c->window;
            }
        }
    }

    XSync(dpy, False);

    XEvent ev;
    while (XCheckMaskEvent(dpy, EnterWindowMask, &ev))
        ;
}

void
run(void)
{
    XSync(dpy, False);

    /* main event loop */
    XEvent ev;
    while (running && !XNextEvent(dpy, &ev)) {
        if (handler[ev.type]) {
            handler[ev.type](&ev); /* call handler */
        }
    }
}

void
scan(void)
{
    unsigned int num;
    Window d1, d2, *wins = NULL;
    if (!XQueryTree(dpy, root_window, &d1, &d2, &wins, &num)) {
        return;
    }

    XWindowAttributes wa;
    for (unsigned int i = 0; i < num; i++) {
        if (!XGetWindowAttributes(dpy, wins[i], &wa)
            || wa.override_redirect
            || XGetTransientForHint(dpy, wins[i], &d1))
        {
            continue;
        }

        if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState) {
            manage(wins[i], &wa);
        }
    }

    for (unsigned int i = 0; i < num; i++) { /* now the transients */
        if (!XGetWindowAttributes(dpy, wins[i], &wa)) {
            continue;
        }

        if (XGetTransientForHint(dpy, wins[i], &d1)
            && (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
        {
            manage(wins[i], &wa);
        }
    }

    if (wins) {
        XFree(wins);
    }
}

void
sendmon(Client* c, Monitor* m)
{
    if (c->monitor == m) {
        return;
    }

    unfocus(c, 1);
    detach_client(c);
    detachstack(c);
    c->monitor = m;
    c->tags = current_tags(m); /* assign tags of target monitor */
    attach(c);
    attachstack(c);
    focus(NULL);
    arrange(NULL);
}

void
setclientstate(Client* c, long state)
{
    long data[] = { state, None };

    XChangeProperty(dpy, c->window, wmatom[WMState], wmatom[WMState], 32,
                    PropModeReplace, (unsigned char*)data, 2);
}

int
sendevent(Client* c, Atom proto)
{
    int n;
    Atom* protocols;
    int exists = 0;

    if (XGetWMProtocols(dpy, c->window, &protocols, &n)) {
        while (!exists && n--) {
            exists = protocols[n] == proto;
        }

        XFree(protocols);
    }

    if (exists) {
        XEvent ev;
        ev.type = ClientMessage;
        ev.xclient.window = c->window;
        ev.xclient.message_type = wmatom[WMProtocols];
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = proto;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, c->window, False, NoEventMask, &ev);
    }

    return exists;
}

void
setfocus(Client* c)
{
    if (!c->neverfocus) {
        XSetInputFocus(dpy, c->window, RevertToPointerRoot, CurrentTime);
        XChangeProperty(dpy, root_window, netatom[NetActiveWindow],
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char*) &(c->window), 1);
    }

    sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client* c, int fullscreen)
{
    if (fullscreen && !c->isfullscreen) {
        XChangeProperty(dpy, c->window, netatom[NetWMState], XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
        c->isfullscreen = 1;
        c->oldstate = c->isfloating;
        c->old_border_width = c->border_width;
        c->border_width = 0;
        c->isfloating = 1;
        resizeclient(c, c->monitor->mx, c->monitor->my, c->monitor->mw, c->monitor->mh);
        XRaiseWindow(dpy, c->window);
    } else if (!fullscreen && c->isfullscreen){
        XChangeProperty(dpy, c->window, netatom[NetWMState], XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)0, 0);
        c->isfullscreen = 0;
        c->isfloating = c->oldstate;
        c->border_width = c->old_border_width;
        c->x = c->oldx;
        c->y = c->oldy;
        c->w = c->oldw;
        c->h = c->oldh;
        resizeclient(c, c->x, c->y, c->w, c->h);
        arrange(c->monitor);
    }
}

void
setgaps(const Arg* arg)
{
    if ((arg->i == 0) || (selected_monitor->gappx + arg->i < 0)) {
        selected_monitor->gappx = 0;
    } else {
        selected_monitor->gappx += arg->i;
    }

    arrange(selected_monitor);
}

void
setlayout(const Arg* arg)
{
    // Sanity check.
    if (!arg || !arg->v) {
        return;
    }

    if (arg->v == current_layout(selected_monitor)) {
        return;
    }

    set_layout(selected_monitor, arg->v);
    copy_layout_symbol(selected_monitor);

    if (selected_monitor->selected_client) {
        arrange(selected_monitor);
    } else {
        drawbar(selected_monitor);
    }
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg* arg)
{
    if (!arg || !current_layout(selected_monitor)->arrange) {
        return;
    }

    const float f = arg->f < 1.0
                    ? arg->f + selected_monitor->mfact
                    : arg->f - 1.0;

    if (f < 0.05 || f > 0.95) {
        return;
    }

    selected_monitor->mfact = f;
    arrange(selected_monitor);
}

void
setup(void)
{
    XSetWindowAttributes wa;
    Atom utf8string;

    /* clean up any zombies immediately */
    sigchld(0);

    /* init screen */
    screen = DefaultScreen(dpy);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);
    root_window = RootWindow(dpy, screen);
    drw = drw_create(dpy, screen, root_window, sw, sh);
    if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
        die("no fonts could be loaded.");
    lrpad = drw->fonts->h;
    bh = drw->fonts->h + 2;
    updategeom();
    /* init atoms */
    utf8string = XInternAtom(dpy, "UTF8_STRING", False);
    wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
    wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
    netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
    netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
    netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
    netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    /* init cursors */
    cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
    cursor[CurResize] = drw_cur_create(drw, XC_sizing);
    cursor[CurMove] = drw_cur_create(drw, XC_fleur);
    /* init appearance */
    scheme = ecalloc(LENGTH(colors), sizeof(Clr*));
    for (size_t i = 0; i < LENGTH(colors); i++) {
        scheme[i] = drw_scm_create(drw, colors[i], 3);
    }
    /* init bars */
    updatebars();
    updatestatus();
    /* supporting window for NetWMCheck */
    wmcheckwin = XCreateSimpleWindow(dpy, root_window, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
        PropModeReplace, (unsigned char*) &wmcheckwin, 1);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
        PropModeReplace, (unsigned char*) "dwm", 3);
    XChangeProperty(dpy, root_window, netatom[NetWMCheck], XA_WINDOW, 32,
        PropModeReplace, (unsigned char*) &wmcheckwin, 1);
    /* EWMH support per view */
    XChangeProperty(dpy, root_window, netatom[NetSupported], XA_ATOM, 32,
        PropModeReplace, (unsigned char*) netatom, NetLast);
    XDeleteProperty(dpy, root_window, netatom[NetClientList]);
    /* select events */
    wa.cursor = cursor[CurNormal]->cursor;
    wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
        |ButtonPressMask|PointerMotionMask|EnterWindowMask
        |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
    XChangeWindowAttributes(dpy, root_window, CWEventMask|CWCursor, &wa);
    XSelectInput(dpy, root_window, wa.event_mask);
    grabkeys();
    focus(NULL);
}


void
seturgent(Client* c, int urg)
{
    c->isurgent = urg;

    XWMHints* wmh;
    if (!(wmh = XGetWMHints(dpy, c->window))) {
        return;
    }

    wmh->flags = urg
                 ? (wmh->flags | XUrgencyHint)
                 : (wmh->flags & ~XUrgencyHint);

    XSetWMHints(dpy, c->window, wmh);
    XFree(wmh);
}

void
showhide(Client* c)
{
    if (!c) {
        return;
    }

    if (ISVISIBLE(c)) {
        /* show clients top down */
        XMoveWindow(dpy, c->window, c->x, c->y);
        if ((!current_layout(c->monitor)->arrange || c->isfloating) && !c->isfullscreen) {
            resize(c, c->x, c->y, c->w, c->h, 0);
        }
        showhide(c->stack_next);
    } else {
        /* hide clients bottom up */
        showhide(c->stack_next);
        XMoveWindow(dpy, c->window, WIDTH(c) * -2, c->y);
    }
}

void
sigchld(int unused)
{
    (void) unused;

    if (signal(SIGCHLD, sigchld) == SIG_ERR) {
        die("can't install SIGCHLD handler:");
    }

    while (0 < waitpid(-1, NULL, WNOHANG))
        ;
}

void
spawn(const Arg* arg)
{
    if (arg->v == dmenucmd) {
        dmenumon[0] = '0' + selected_monitor->num;
    }

    if (fork() == 0) {
        if (dpy) {
            close(ConnectionNumber(dpy));
        }

        setsid();
        execvp(((char**)arg->v)[0], (char**)arg->v);
        fprintf(stderr, "dwm: execvp %s", ((char**)arg->v)[0]);
        perror(" failed");
        exit(EXIT_SUCCESS);
    }
}

void
tag(const Arg* arg)
{
    const unsigned int index = arg->ui;
    const unsigned int tags_to_move_to = (1 << index) & TAGMASK;

    Client* selected_client = selected_monitor->selected_client;

    if (selected_client && tags_to_move_to) {
        selected_client->tags = tags_to_move_to;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void
tagmon(const Arg* arg)
{
    if (!selected_monitor->selected_client || !monitors->next) {
        return;
    }

    sendmon(selected_monitor->selected_client, dirtomon(arg->i));
}

void
tile(Monitor* m)
{
    unsigned int mw, my, ty;

    const unsigned int n = count_tiled_clients(m);
    if (n == 0) {
        return;
    }

    if (n > m->masters_count) {
        mw = m->masters_count
             ? m->ww * m->mfact
             : 0;
    } else {
        mw = m->ww - m->gappx;
    }

    my = ty = m->gappx;

    unsigned int client_index = 0;
    Client* c = nexttiled(m->clients);

    while (c != NULL) {
        if (client_index < m->masters_count) {
            const unsigned int h = (m->wh - my) / (MIN(n, m->masters_count) - client_index) - m->gappx;
            resize(c, m->wx + m->gappx, m->wy + my,
                   mw - (2*c->border_width) - m->gappx, h - (2*c->border_width), 0);
            my += HEIGHT(c) + m->gappx;
        } else {
            const unsigned int h = (m->wh - ty) / (n - client_index) - m->gappx;
            resize(c, m->wx + mw + m->gappx, m->wy + ty,
                   m->ww - mw - (2*c->border_width) - 2*m->gappx, h - (2*c->border_width), 0);
            ty += HEIGHT(c) + m->gappx;
        }

        ++client_index;
        c = nexttiled(c->next);
    }
}

void
togglebar(const Arg* arg)
{
    (void) arg;

    selected_monitor->showbar = !selected_monitor->showbar;
    updatebarpos(selected_monitor);
    XMoveResizeWindow(dpy, selected_monitor->bar_window,
                      selected_monitor->wx, selected_monitor->by, selected_monitor->ww, bh);
    arrange(selected_monitor);
}

void
togglefloating(const Arg* arg)
{
    (void) arg;

    Client* selected_client = selected_monitor->selected_client;

    if (!selected_client) {
        return;
    }

    if (selected_client->isfullscreen) {
        /* no support for fullscreen windows */
        return;
    }

    selected_client->isfloating = !selected_client->isfloating || selected_client->isfixed;

    if (selected_client->isfloating) {
        resize(selected_client, selected_client->x, selected_client->y,
               selected_client->w, selected_client->h, 0);
    }

    arrange(selected_monitor);
}

void
toggletag(const Arg* arg)
{
    Client* selected_client = selected_monitor->selected_client;
    if (!selected_client) {
        return;
    }

    const unsigned int index = arg->ui;
    const unsigned int tag_to_toggle = (1 << index) & TAGMASK;
    const unsigned int updated_tags = selected_client->tags ^ tag_to_toggle;

    if (updated_tags) {
        selected_client->tags = updated_tags;
        focus(NULL);
        arrange(selected_monitor);
    }
}

void
toggleview(const Arg* arg)
{
    const unsigned int index = arg->ui;
    const unsigned int tag_to_toggle = (1 << index) & TAGMASK;
    const unsigned int updated_tags = current_tags(selected_monitor) ^ tag_to_toggle;

    if (updated_tags) {
        set_tags(selected_monitor, updated_tags);
        focus(NULL);
        arrange(selected_monitor);
    }
}

void
unfocus(Client* c, int setfocus)
{
    if (!c) {
        return;
    }

    grabbuttons(c, 0);
    XSetWindowBorder(dpy, c->window, scheme[SchemeNorm][ColBorder].pixel);
    if (setfocus) {
        XSetInputFocus(dpy, root_window, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root_window, netatom[NetActiveWindow]);
    }
}

void
unmanage(Client* c, int destroyed)
{
    Monitor* m = c->monitor;
    XWindowChanges wc;

    detach_client(c);
    detachstack(c);

    if (!destroyed) {
        wc.border_width = c->old_border_width;
        XGrabServer(dpy); /* avoid race conditions */
        XSetErrorHandler(xerrordummy);
        XConfigureWindow(dpy, c->window, CWBorderWidth, &wc); /* restore border */
        XUngrabButton(dpy, AnyButton, AnyModifier, c->window);
        setclientstate(c, WithdrawnState);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }

    free(c);
    focus(NULL);
    updateclientlist();
    arrange(m);
}

void
unmapnotify(XEvent* e)
{
    XUnmapEvent* ev = &e->xunmap;

    Client* c;
    if ((c = window_to_client(ev->window))) {
        if (ev->send_event) {
            setclientstate(c, WithdrawnState);
        } else {
            unmanage(c, 0);
        }
    }
}

void
updatebars(void)
{
    XSetWindowAttributes wa = {
        .override_redirect = True,
        .background_pixmap = ParentRelative,
        .event_mask = ButtonPressMask|ExposureMask
    };

    XClassHint hint = {"dwm", "dwm"};
    for (Monitor* m = monitors; m != NULL; m = m->next) {
        if (m->bar_window) {
            continue;
        }

        m->bar_window = XCreateWindow(dpy, root_window, m->wx, m->by, m->ww, bh, 0, DefaultDepth(dpy, screen),
                CopyFromParent, DefaultVisual(dpy, screen),
                CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
        XDefineCursor(dpy, m->bar_window, cursor[CurNormal]->cursor);
        XMapRaised(dpy, m->bar_window);
        XSetClassHint(dpy, m->bar_window, &hint);
    }
}

void
updatebarpos(Monitor* m)
{
    m->wy = m->my;
    m->wh = m->mh;

    if (m->showbar) {
        m->wh -= bh;
        m->by = m->topbar ? m->wy : m->wy + m->wh;
        m->wy = m->topbar ? m->wy + bh : m->wy;
    } else {
        m->by = -bh;
    }
}

void
updateclientlist()
{
    XDeleteProperty(dpy, root_window, netatom[NetClientList]);
    for (Monitor* m = monitors; m; m = m->next) {
        for (Client* c = m->clients; c; c = c->next) {
            XChangeProperty(dpy, root_window, netatom[NetClientList],
                            XA_WINDOW, 32, PropModeAppend,
                            (unsigned char*) &(c->window), 1);
        }
    }
}

int
updategeom(void)
{
    int dirty = 0;

#ifdef XINERAMA
    if (XineramaIsActive(dpy)) {
        int screens_count;
        XineramaScreenInfo* info = XineramaQueryScreens(dpy, &screens_count);

        /* only consider unique geometries as separate screens */
        XineramaScreenInfo* unique = ecalloc(screens_count, sizeof(XineramaScreenInfo));

        int unique_screens_count = 0;
        for (int i = 0; i < screens_count; i++) {
            if (isuniquegeom(unique, unique_screens_count, &info[i])) {
                memcpy(&unique[unique_screens_count++], &info[i], sizeof(XineramaScreenInfo));
            }
        }

        XFree(info);
        screens_count = unique_screens_count;

        int monitors_count = 0;
        for (Monitor* m = monitors; m != NULL; m = m->next) {
            ++monitors_count;
        }

        if (monitors_count <= screens_count) { /* new monitors available */
            const int new_monitors_count = screens_count - monitors_count;

            for (int i = 0; i < new_monitors_count; i++) {
                // FIXME(vlad): do not search for the last monitor at each iteration.

                Monitor* last_monitor = monitors;
                while (last_monitor && last_monitor->next) {
                    last_monitor = last_monitor->next;
                }

                if (last_monitor) {
                    last_monitor->next = createmon();
                } else {
                    monitors = createmon();
                }
            }

            Monitor* m = monitors;
            for (int i = 0; i < screens_count && m; m = m->next, i++) {
                if (i >= monitors_count
                    || unique[i].x_org != m->mx || unique[i].y_org != m->my
                    || unique[i].width != m->mw || unique[i].height != m->mh)
                {
                    dirty = 1;
                    m->num = i;
                    m->mx = m->wx = unique[i].x_org;
                    m->my = m->wy = unique[i].y_org;
                    m->mw = m->ww = unique[i].width;
                    m->mh = m->wh = unique[i].height;
                    updatebarpos(m);
                }
            }
        } else { /* less monitors available */
            for (int i = screens_count; i < monitors_count; i++) {
                Monitor* last_monitor = monitors;
                while (last_monitor && last_monitor->next) {
                    last_monitor = last_monitor->next;
                }

                Client* c;
                while ((c = last_monitor->clients)) {
                    dirty = 1;
                    last_monitor->clients = c->next;
                    detachstack(c);
                    c->monitor = monitors;
                    attach(c);
                    attachstack(c);
                }

                if (last_monitor == selected_monitor) {
                    selected_monitor = monitors;
                }

                cleanupmon(last_monitor);
            }
        }

        free(unique);
    } else
#endif /* XINERAMA */

    { /* default monitor setup */
        if (!monitors) {
            monitors = createmon();
        }

        if (monitors->mw != sw || monitors->mh != sh) {
            dirty = 1;
            monitors->mw = monitors->ww = sw;
            monitors->mh = monitors->wh = sh;
            updatebarpos(monitors);
        }
    }

    if (dirty) {
        selected_monitor = monitors;
        selected_monitor = window_to_monitor(root_window);
    }

    return dirty;
}

void
updatenumlockmask(void)
{
    numlockmask = 0;

    XModifierKeymap* modmap = XGetModifierMapping(dpy);
    for (size_t i = 0; i < 8; i++) {
        for (int j = 0; j < modmap->max_keypermod; j++) {
            if (modmap->modifiermap[i * modmap->max_keypermod + j] == XKeysymToKeycode(dpy, XK_Num_Lock)) {
                numlockmask = (1 << i);
            }
        }
    }

    XFreeModifiermap(modmap);
}

void
updatesizehints(Client* c)
{
    long msize;
    XSizeHints size;

    if (!XGetWMNormalHints(dpy, c->window, &size, &msize)) {
        /* size is uninitialized, ensure that size.flags aren't used */
        size.flags = PSize;
    }

    if (size.flags & PBaseSize) {
        c->basew = size.base_width;
        c->baseh = size.base_height;
    } else if (size.flags & PMinSize) {
        c->basew = size.min_width;
        c->baseh = size.min_height;
    } else {
        c->basew = c->baseh = 0;
    }

    if (size.flags & PResizeInc) {
        c->incw = size.width_inc;
        c->inch = size.height_inc;
    } else {
        c->incw = c->inch = 0;
    }

    if (size.flags & PMaxSize) {
        c->maxw = size.max_width;
        c->maxh = size.max_height;
    } else {
        c->maxw = c->maxh = 0;
    }

    if (size.flags & PMinSize) {
        c->minw = size.min_width;
        c->minh = size.min_height;
    } else if (size.flags & PBaseSize) {
        c->minw = size.base_width;
        c->minh = size.base_height;
    } else {
        c->minw = c->minh = 0;
    }

    if (size.flags & PAspect) {
        c->mina = (float)size.min_aspect.y / size.min_aspect.x;
        c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
    } else {
        c->maxa = c->mina = 0.0;
    }

    c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
}

void
updatestatus(void)
{
    if (!gettextprop(root_window, XA_WM_NAME, status_text, sizeof(status_text))) {
        strcpy(status_text, "dwm-"VERSION);
    }

    drawbar(selected_monitor);
}

void
updatetitle(Client* c)
{
    if (!gettextprop(c->window, netatom[NetWMName], c->name, sizeof c->name)) {
        gettextprop(c->window, XA_WM_NAME, c->name, sizeof c->name);
    }

    if (c->name[0] == '\0') { /* hack to mark broken clients */
        strcpy(c->name, broken);
    }
}

void
updatewindowtype(Client* c)
{
    Atom state = getatomprop(c, netatom[NetWMState]);
    Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

    if (state == netatom[NetWMFullscreen]) {
        setfullscreen(c, 1);
    }

    if (wtype == netatom[NetWMWindowTypeDialog]) {
        c->isfloating = 1;
    }
}

void
updatewmhints(Client* c)
{
    XWMHints* wmh = XGetWMHints(dpy, c->window);
    if (!wmh) {
        return;
    }

    if (c == selected_monitor->selected_client && wmh->flags & XUrgencyHint) {
        wmh->flags &= ~XUrgencyHint;
        XSetWMHints(dpy, c->window, wmh);
    } else {
        c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
    }

    if (wmh->flags & InputHint) {
        c->neverfocus = !wmh->input;
    } else {
        c->neverfocus = 0;
    }

    XFree(wmh);
}

void
view(const Arg* arg)
{
    const unsigned int index = arg->ui;
    const unsigned int requested_tag = (1 << index) & TAGMASK;

    if (requested_tag == current_tags(selected_monitor)) {
        return;
    }

    swap_selected_tags(selected_monitor);

    if (requested_tag) {
        set_tags(selected_monitor, requested_tag);
    }

    set_layout_index(selected_monitor, index);

    focus(NULL);
    arrange(selected_monitor);
}

Client*
window_to_client(Window window)
{
    for (Monitor* m = monitors; m != NULL; m = m->next) {
        for (Client* c = m->clients; c != NULL; c = c->next) {
            if (c->window == window) {
                return c;
            }
        }
    }

    return NULL;
}

Monitor*
window_to_monitor(Window window)
{
    int x, y;
    if (window == root_window && getrootptr(&x, &y)) {
        return recttomon(x, y, 1, 1);
    }

    for (Monitor* m = monitors; m != NULL; m = m->next) {
        if (window == m->bar_window) {
            return m;
        }
    }

    Client* c;
    if ((c = window_to_client(window))) {
        return c->monitor;
    }

    return selected_monitor;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display* display, XErrorEvent* ee)
{
    if (ee->error_code == BadWindow
    || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
    || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
    || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
    || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
    || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
    || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
    {
        return 0;
    }

    fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
            ee->request_code, ee->error_code);

    return xerrorxlib(display, ee); /* may call exit */
}

int
xerrordummy(Display* display, XErrorEvent* ee)
{
    (void) display;
    (void) ee;

    return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display* display, XErrorEvent* ee)
{
    (void) display;
    (void) ee;

    die("dwm: another window manager is already running");
    return -1;
}

void
zoom(const Arg* arg)
{
    (void) arg;

    Client* selected_client = selected_monitor->selected_client;

    if (!current_layout(selected_monitor)->arrange
        || (selected_client && selected_client->isfloating))
    {
        return;
    }

    if (selected_client == nexttiled(selected_monitor->clients)) {
        if (!selected_client || !(selected_client = nexttiled(selected_client->next))) {
            return;
        }
    }

    pop(selected_client);
}

int
main(int argc, const char* argv[])
{
    if (argc == 2 && !strcmp("-v", argv[1])) {
        die("dwm-"VERSION);
    } else if (argc != 1) {
        die("usage: dwm [-v]");
    } if (!setlocale(LC_CTYPE, "") || !XSupportsLocale()) {
        fputs("warning: no locale support\n", stderr);
    }

    if (!(dpy = XOpenDisplay(NULL))) {
        die("dwm: cannot open display");
    }

    die_if_other_wm_is_running();

    setup();

#ifdef __OpenBSD__
    if (pledge("stdio rpath proc exec", NULL) == -1) {
        die("pledge");
    }
#endif /* __OpenBSD__ */

    scan();
    run();
    cleanup();

    XCloseDisplay(dpy);
    return EXIT_SUCCESS;
}
