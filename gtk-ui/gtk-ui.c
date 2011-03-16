#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include <fcntl.h>
#include <linux/input.h>
#include <linux/fb.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>

#include <gtk/gtk.h>

#define IMAGE_WIDTH  640
#define IMAGE_HEIGHT 480

#define EV_PRESSED  1
#define EV_RELEASED 0

static GdkPixmap *pixmap = NULL;
guchar rgbbuf[IMAGE_WIDTH * IMAGE_HEIGHT * 3];
static int currently_drawing = 0;

/* Framebuffer */
guchar*                  bits;
int                      bpp;    /* byte per pixel */
int                      stride; /* size of stride in pixel */
struct fb_var_screeninfo vi;
struct fb_fix_screeninfo fi;

/* Input events */
static char INPUT_DEVICE[PATH_MAX] = "/dev/input/event2";
static int inputfd = -1;
static int xmin, xmax;
static int ymin, ymax;

static gboolean configure_event(GtkWidget *widget, GdkEventConfigure *event)
{
    return TRUE;
}

void *do_draw(void *ptr)
{
    siginfo_t info;
    sigset_t sigset;

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);

    while (1) {
        while (sigwaitinfo(&sigset, &info) > 0) {
            currently_drawing = 1;

            int width, height;
            gdk_threads_enter();
            gdk_drawable_get_size(pixmap, &width, &height);
            gdk_threads_leave();
            
            memcpy(rgbbuf, 
                   bits + (vi.xoffset + vi.yoffset*vi.xres_virtual)*bpp, 
                   vi.yres*stride*bpp);

            cairo_surface_t *cst = 
                cairo_image_surface_create_for_data(rgbbuf,
                  CAIRO_FORMAT_RGB16_565, IMAGE_WIDTH, IMAGE_HEIGHT, 
                  stride*bpp);

            //When dealing with gdkPixmap's, we need to make sure not to
            //access them from outside gtk_main().
            gdk_threads_enter();

            cairo_t *cr_pixmap = gdk_cairo_create(pixmap);
            cairo_set_source_surface(cr_pixmap, cst, 0, 0);
            cairo_paint(cr_pixmap);
            cairo_destroy(cr_pixmap);

            gdk_threads_leave();

            cairo_surface_destroy(cst);

            currently_drawing = 0;
        }
    }
}

gboolean timer_exe(GtkWidget *widget)
{
    static int first_time = 1;
    static pthread_t thread_info;
    int width, height;

    int drawing_status = g_atomic_int_get(&currently_drawing);

    if(first_time == 1) {
        int  iret;
        iret = pthread_create(&thread_info, NULL, do_draw, NULL);
    }

    if(drawing_status == 0) {
        pthread_kill(thread_info, SIGALRM);
    }

    gdk_drawable_get_size(pixmap, &width, &height);
    gtk_widget_queue_draw_area(widget, 0, 0, width, height);

    first_time = 0;
    return TRUE;
}

static gboolean expose_event(GtkWidget *widget, GdkEventExpose *event)
{
    cairo_t *cr = gdk_cairo_create(widget->window);
    gdk_cairo_set_source_pixmap(cr, pixmap, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    return FALSE;
}

static void init_input_device()
{
    struct input_absinfo info;
    
    if((inputfd = open(INPUT_DEVICE, O_RDWR)) == -1) {
        printf("Cannot open input device %s\n", INPUT_DEVICE);
        exit(EXIT_FAILURE);
    }
    
    // Get the Range of X and Y
    if(ioctl(inputfd, EVIOCGABS(ABS_X), &info)) {
        printf("Cannot get ABS_X info, %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    xmin = info.minimum;
    xmax = info.maximum;
    if (xmax) {
        printf("Touch device xmin=%d xmax=%d\n", xmin, xmax);
    } else {
        printf("Touch device has no xmax: using emulator mode\n");
    }

    if(ioctl(inputfd, EVIOCGABS(ABS_Y), &info)) {
        printf("Cannot get ABS_Y, %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ymin = info.minimum;
    ymax = info.maximum;
    if (ymax)
        printf("Touch device ymin=%d ymax=%d\n", ymin, ymax);
    else
        printf("Touch device has no ymax: using emulator mode\n");
}

static void cleanup_input()
{
    if(inputfd != -1) {
        close(inputfd);
    }
}

void injectKeyEvent(unsigned int code, unsigned int value)
{
    struct input_event ev;
    
    memset(&ev, 0, sizeof(ev));
    gettimeofday(&ev.time, 0);
    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;
    if(write(inputfd, &ev, sizeof(ev)) < 0) {
        printf("Event failed, %s\n", strerror(errno));
    }

    /*gettimeofday(&ev.time,0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if(write(inputfd, &ev, sizeof(ev)) < 0) {
        printf("Event failed, %s\n", strerror(errno));
    }*/
}

void injectTouchEvent(int down, int x, int y)
{
    struct input_event ev;

    /* Re-calculate the final x and y if xmax/ymax are specified */
    if (xmax) x = xmin + (x * (xmax - xmin)) / (vi.xres);
    if (ymax) y = ymin + (y * (ymax - ymin)) / (vi.yres);
    
    memset(&ev, 0, sizeof(ev));

    // Send a BTN_TOUCH
    gettimeofday(&ev.time,0);
    ev.type = EV_KEY;
    ev.code = BTN_TOUCH;
    ev.value = down;
    if(write(inputfd, &ev, sizeof(ev)) < 0) {
        printf("Write event failed, %s\n", strerror(errno));
    }

    gettimeofday(&ev.time,0);
    ev.type = EV_ABS;
    ev.code = ABS_X;
    ev.value = x;
    if(write(inputfd, &ev, sizeof(ev)) < 0) {
        printf("Write event failed, %s\n", strerror(errno));
    }

    gettimeofday(&ev.time,0);
    ev.type = EV_ABS;
    ev.code = ABS_Y;
    ev.value = y;
    if(write(inputfd, &ev, sizeof(ev)) < 0) {
        printf("Write event failed, %s\n", strerror(errno));
    }

    // Finally send the SYN
    gettimeofday(&ev.time,0);
    ev.type = EV_SYN;
    ev.code = 0;
    ev.value = 0;
    if(write(inputfd, &ev, sizeof(ev)) < 0) {
        printf("Write event failed, %s\n", strerror(errno));
    }

    /*printf("injectTouchEvent (x=%d, y=%d, down=%d)\n", x, y, down);*/
}

static gboolean button_press_event(GtkWidget *widget, GdkEventButton *event)
{
    if (event->button == 1) {
        injectTouchEvent(1, event->x, event->y);
    }

    return TRUE;
}

static gboolean button_release_event(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    if (event->button == 1) {
        injectTouchEvent(0, event->x, event->y);
    }

    return TRUE;
}

static gboolean motion_notify_event(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
    int x, y;
    GdkModifierType state;

    if (event->is_hint) {
        gdk_window_get_pointer(event->window, &x, &y, &state);
    } else {
        x = event->x;
        y = event->y;
        state = event->state;
    }

    if (state & GDK_BUTTON1_MASK) {
        injectTouchEvent(1, x, y);
    } else {
        injectTouchEvent(0, x, y);
    }

    return TRUE;
}

static void destroy(GtkWidget *widget, gpointer data)
{
    cleanup_input();
    gtk_main_quit();
}

static void back_button_clicked()
{
    printf("Back button pressed\n");
    injectKeyEvent(KEY_BACKSPACE, EV_PRESSED);
    injectKeyEvent(KEY_BACKSPACE, EV_RELEASED);
}

static void home_button_clicked()
{
    printf("Home button pressed\n");
    injectKeyEvent(KEY_HOME, EV_PRESSED);
    injectKeyEvent(KEY_HOME, EV_RELEASED);
}

static void menu_button_clicked()
{
    printf("Menu button pressed\n");
    injectKeyEvent(KEY_LEFTMETA, EV_PRESSED);
    injectKeyEvent(KEY_LEFTMETA, EV_RELEASED); //0x52
}

int main(int argc, char *argv[])
{
    int fd;

    GtkWidget *window;
    GtkWidget *drawing_area;
    GtkWidget *vbox;
    GtkWidget *hbox;
    GtkWidget *button;
    
    /* Block SIGALRM in the main thread */
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &sigset, NULL);
    
    if (!g_thread_supported()) {
        g_thread_init(NULL);
    }

    gdk_threads_init();
    gdk_threads_enter();
    
    gtk_init(&argc, &argv);

    /* Framebuffer */
    
    /* Open framebuffer */
    if (0 > (fd = open("/dev/fb0", O_RDWR))) {
        printf("Failed to open fb\n");
        return -1;
    }

    /* Get fixed information */
    if(0 > ioctl(fd, FBIOGET_FSCREENINFO, &fi)) {
        printf("Failed to get fixed info\n");
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

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_widget_show(vbox);

    drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(GTK_WIDGET(drawing_area), vi.xres, vi.yres);
    gtk_box_pack_start(GTK_BOX(vbox), drawing_area, TRUE, TRUE, 0);
    gtk_widget_show(drawing_area);
    
    init_input_device();

    /* Events */
    gtk_widget_set_events(drawing_area, GDK_EXPOSURE_MASK
                          | GDK_LEAVE_NOTIFY_MASK
                          | GDK_BUTTON_PRESS_MASK
                          | GDK_BUTTON_RELEASE_MASK
                          | GDK_POINTER_MOTION_MASK
                          | GDK_POINTER_MOTION_HINT_MASK);

    g_signal_connect(drawing_area, "motion-notify-event",
                     G_CALLBACK (motion_notify_event), NULL);
    g_signal_connect(drawing_area, "button-press-event",
                     G_CALLBACK(button_press_event), NULL);
    g_signal_connect(drawing_area, "button-release-event",
                     G_CALLBACK(button_release_event), NULL);
    g_signal_connect(drawing_area, "expose-event",
                     G_CALLBACK(expose_event), NULL);
    g_signal_connect(drawing_area, "configure-event",
                     G_CALLBACK(configure_event), NULL);
                     
    /* Create and add buttons */
    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);
    gtk_widget_show(hbox);
                     
    button = gtk_button_new_with_label("Back");
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    g_signal_connect(button, "clicked",
                     G_CALLBACK(back_button_clicked), NULL);
    gtk_widget_show(button);
    
    button = gtk_button_new_with_label("Home");
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(home_button_clicked), NULL);
    gtk_widget_show(button);
    
    button = gtk_button_new_with_label("Menu");
    gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(menu_button_clicked), NULL);
    gtk_widget_show(button);

    gtk_widget_show_all(window);

    pixmap = gdk_pixmap_new(drawing_area->window, IMAGE_WIDTH, 
                            IMAGE_HEIGHT, -1);

    (void)g_timeout_add(33, (GSourceFunc)timer_exe, drawing_area);

    gtk_main();
    gdk_threads_leave();
    
    return 0;
}
