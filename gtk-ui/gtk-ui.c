

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <linux/input.h>
#include <linux/fb.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>

#include <gtk/gtk.h>

#define IMAGE_WIDTH  640
#define IMAGE_HEIGHT 480

static GdkPixmap *pixmap = NULL;
guchar rgbbuf[IMAGE_WIDTH * IMAGE_HEIGHT * 3];

/* Framebuffer */
guchar*                  bits;
int                      bpp;    /* byte per pixel */
int                      stride; /* size of stride in pixel */
struct fb_var_screeninfo vi;
struct fb_fix_screeninfo fi;

/* Touch events */
static char TOUCH_DEVICE[PATH_MAX] = "/dev/input/event2";
static int touchfd = -1;
static int xmin, xmax;
static int ymin, ymax;

/* Create a new backing pixmap of the appropriate size */
static gboolean
configure_event (GtkWidget *widget, GdkEventConfigure *event)
{
    return TRUE;
}

static void draw_buffer(GtkWidget *widget)
{
    guchar *pos;
    gint x, y;
    pos = rgbbuf;
    unsigned int value;
    for (y = 0; y < vi.yres; y++) {
        for (x = 0; x < vi.xres; x++) {
            value = (bits[(x + vi.xoffset + (vi.yoffset + y)*vi.xres_virtual)*bpp]) | 
                    (bits[(x + vi.xoffset + (vi.yoffset + y)*vi.xres_virtual)*bpp + 1]  << 8);
            *pos++ = (value & 0x001f) << 3;
            *pos++ = ((value & 0x07e0) >> 5) << 2;
            *pos++ = ((value & 0xf800) >> 11) << 3;
        }
    }
                     
    gdk_draw_rgb_image(widget->window, widget->style->fg_gc[GTK_STATE_NORMAL],
                     0, 0, vi.xres, vi.yres,
                     GDK_RGB_DITHER_MAX, rgbbuf, vi.xres * 3);
                     
    printf("exposed!\n");
}

/* Redraw the screen from the backing pixmap */
static gboolean
expose_event (GtkWidget *widget, GdkEventExpose *event)
{
    draw_buffer(widget);

    return FALSE;
}

static void init_touch()
{
    struct input_absinfo info;
    
    if((touchfd = open(TOUCH_DEVICE, O_RDWR)) == -1) {
        printf("cannot open touch device %s\n", TOUCH_DEVICE);
        exit(EXIT_FAILURE);
    }
    
    // Get the Range of X and Y
    if(ioctl(touchfd, EVIOCGABS(ABS_X), &info)) {
        printf("cannot get ABS_X info, %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    xmin = info.minimum;
    xmax = info.maximum;
    if (xmax)
        printf("touchscreen xmin=%d xmax=%d\n", xmin, xmax);
    else
        printf("touchscreen has no xmax: using emulator mode\n");

    if(ioctl(touchfd, EVIOCGABS(ABS_Y), &info)) {
        printf("cannot get ABS_Y, %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ymin = info.minimum;
    ymax = info.maximum;
    if (ymax)
        printf("touchscreen ymin=%d ymax=%d\n", ymin, ymax);
    else
        printf("touchscreen has no ymax: using emulator mode\n");
}

static void cleanup_touch()
{
    if(touchfd != -1) {
        close(touchfd);
    }
}

void injectTouchEvent(int down, int x, int y)
{
    struct input_event ev;

    // Re-calculate the final x and y if xmax/ymax are specified
    if (xmax) x = xmin + (x * (xmax - xmin)) / (vi.xres);
    if (ymax) y = ymin + (y * (ymax - ymin)) / (vi.yres);
    
    memset(&ev, 0, sizeof(ev));

    // Then send a BTN_TOUCH
    gettimeofday(&ev.time,0);
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = down;
    if(write(touchfd, &ev, sizeof(ev)) < 0) {
        printf("write event failed, %s\n", strerror(errno));
    }

    // Then send the X
    gettimeofday(&ev.time,0);
    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = x;
    if(write(touchfd, &ev, sizeof(ev)) < 0)
    {
        printf("write event failed, %s\n", strerror(errno));
    }

    // Then send the Y
    gettimeofday(&ev.time,0);
    ev.type = EV_ABS;
    ev.code = ABS_Y;
    ev.value = y;
    if(write(touchfd, &ev, sizeof(ev)) < 0) {
        printf("write event failed, %s\n", strerror(errno));
    }

    // Finally send the SYN
    gettimeofday(&ev.time,0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if(write(touchfd, &ev, sizeof(ev)) < 0) {
        printf("write event failed, %s\n", strerror(errno));
    }

    printf("injectTouchEvent (x=%d, y=%d, down=%d)\n", x , y, down);
}

static gboolean button_press_event(GtkWidget      *widget,
                                   GdkEventButton *event )
{
    if (event->button == 1) {
        injectTouchEvent(1, event->x, event->y);
    }

    return TRUE;
}

static gboolean button_release_event(GtkWidget      *widget,
                                     GdkEventButton *event )
{
    if (event->button == 1) {
        //injectTouchEvent(0, event->x, event->y);
    }
    injectTouchEvent(0, event->x, event->y);

    return TRUE;
}

static gboolean motion_notify_event(GtkWidget *widget,
                                    GdkEventMotion *event)
{
    int x, y;
    GdkModifierType state;

    if (event->is_hint) {
        gdk_window_get_pointer (event->window, &x, &y, &state);
    } else {
        x = event->x;
        y = event->y;
        state = event->state;
    }

    if (state & GDK_BUTTON1_MASK)
        injectTouchEvent(1, x, y);
    else
        injectTouchEvent(0, x, y);

    draw_buffer(widget);

    return TRUE;
}

static void destroy(GtkWidget *widget, gpointer data)
{
    cleanup_touch();
    gtk_main_quit();
}

int main(int argc, char *argv[])
{
    int                      fd;

    int should_quit = 0;
    void* curbits;

    GtkWidget *window;
    GtkWidget *drawing_area;
    //GtkWidget *vbox;
    
    gtk_init(&argc, &argv);
    
    
    /* Deal with framebuffer */
    
    /* Open framebuffer */
    if (0 > (fd = open("/dev/fb0", O_RDWR))) {
        printf("Fail to open fb\n");
        return -1;
    }

    /* Get fixed information */
    if(0 > ioctl(fd, FBIOGET_FSCREENINFO, &fi)) {
        printf("Fail to get fixed info\n");
        return -1;
    }

    /* Get variable information */
    if(0 > ioctl(fd, FBIOGET_VSCREENINFO, &vi)) {
        printf("Failed to get variable info\n");
        return -1;
    }

    /* Get raw bits buffer */
    if(MAP_FAILED == (bits = mmap(0, fi.smem_len,
                              PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0))) {
        printf("Failed to mmap fb\n");
        return -1;
    }

    printf("Framebuffer resolution: %d x %d\n", vi.xres, vi.yres);

    
    /* Calculate useful information */
    bpp = vi.bits_per_pixel >> 3;
    stride = fi.line_length / bpp;
    
    /* Do GTK stuff */
    
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title((GtkWindow*)window, "ParallelDroid");
    g_signal_connect(window, "destroy", G_CALLBACK(destroy), NULL);
    
    //vbox = gtk_vbox_new(FALSE, 0);
    //gtk_container_add(GTK_CONTAINER(window), vbox);
    //gtk_widget_show(vbox);
    
    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(GTK_WIDGET(drawing_area), vi.xres, vi.yres);
    //gtk_box_pack_start(GTK_BOX(vbox), drawing_area, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), drawing_area);
    gtk_widget_show(drawing_area);
    
    /* Event signals */
    init_touch();
    
    g_signal_connect(drawing_area, "motion_notify_event",
                     G_CALLBACK (motion_notify_event), NULL);
    g_signal_connect(drawing_area, "button_press_event",
                     G_CALLBACK(button_press_event), NULL);
    g_signal_connect(drawing_area, "button_release_event",
                     G_CALLBACK(button_release_event), NULL);

    gtk_widget_set_events(drawing_area, GDK_EXPOSURE_MASK
                          | GDK_LEAVE_NOTIFY_MASK
                          | GDK_BUTTON_PRESS_MASK
                          | GDK_POINTER_MOTION_MASK
                          | GDK_POINTER_MOTION_HINT_MASK);
    
    g_signal_connect(drawing_area, "expose_event",
                     G_CALLBACK(expose_event), NULL);
    g_signal_connect(drawing_area, "configure_event",
                     G_CALLBACK(configure_event), NULL);
    
    gtk_widget_show(window);
    
    
    gtk_main();
    
    return 0;
}
