/* -*- Mode: c; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil; -*- */

/* TODO
+- non-blocking receiving
+- close socket on exit
+- pass only snd to dopoll()
+*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <time.h>
#include <errno.h>

#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include "config.h"
#include "base.h"
#include "pj.h"
#include "graphics.h"

/* pdreceive includes */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#define SOCKET_ERROR -1




#define MAX_SOURCE_BUF (1024*64)
#define MOUSE_DEVICE_PATH "/dev/input/event0"

#define MAX(a, b) (((a) >= (b)) ? (a) : (b))
#define MIN(a, b) (((a) <  (b)) ? (a) : (b))
#define CLAMP(min, x, max) MIN(MAX(min, x), max)

typedef struct {
    const char *path;
    time_t last_modify_time;
} SourceObject;

struct PJContext_ {
    Graphics *graphics;
    Graphics_LAYOUT layout_backup;
    int is_fullscreen;
    int use_backbuffer;
    struct {
        int x, y;
        int fd;
    } mouse;
    struct {
        int a;
    } snd;
    double time_origin;
    unsigned int frame;         /* TODO: move to graphics */
    struct {
        int debug;
        int render_time;
    } verbose;
    struct {
        int numer;
        int denom;
    } scaling;
};

/* PDRCV */
typedef struct _fdpoll
{
    int fdp_fd;
    char *fdp_outbuf;/*output message buffer*/
    int fdp_outlen;     /*length of output message*/
    int fdp_discard;/*buffer overflow: output message is incomplete, discard it*/
    int fdp_gotsemi;/*last char from input was a semicolon*/
} t_fdpoll;

static int nfdpoll;
static t_fdpoll *fdpoll;
static int maxfd;
static int sockfd;
static int protocol;

static void sockerror(char *s);
static void x_closesocket(int fd);
static void dopoll(PJContext *pj);
#define BUFSIZE 4096



static void addport(int fd)
{
    int nfd = nfdpoll;
    t_fdpoll *fp;
    fdpoll = (t_fdpoll *)realloc(fdpoll,
        (nfdpoll+1) * sizeof(t_fdpoll));
    fp = fdpoll + nfdpoll;
    fp->fdp_fd = fd;
    nfdpoll++;
    if (fd >= maxfd) maxfd = fd + 1;
    fp->fdp_outlen = fp->fdp_discard = fp->fdp_gotsemi = 0;
    if (!(fp->fdp_outbuf = (char*) malloc(BUFSIZE)))
    {
        fprintf(stderr, "out of memory");
        exit(1);
    }
    printf("number_connected %d;\n", nfdpoll);
}

static void rmport(t_fdpoll *x)
{
    int nfd = nfdpoll;
    int i, size = nfdpoll * sizeof(t_fdpoll);
    t_fdpoll *fp;
    for (i = nfdpoll, fp = fdpoll; i--; fp++)
    {
        if (fp == x)
        {
            x_closesocket(fp->fdp_fd);
            free(fp->fdp_outbuf);
            while (i--)
            {
                fp[0] = fp[1];
                fp++;
            }
            fdpoll = (t_fdpoll *)realloc(fdpoll,
                (nfdpoll-1) * sizeof(t_fdpoll));
            nfdpoll--;
            printf("number_connected %d;\n", nfdpoll);
            return;
        }
    }
    fprintf(stderr, "warning: item removed from poll list but not found");
}

static void doconnect(void)
{
    int fd = accept(sockfd, 0, 0);
    if (fd < 0)
        perror("accept");
    else addport(fd);
}

static int tcpmakeoutput(t_fdpoll *x, char *inbuf, int len, PJContext *pj)
{
    int i;
    int outlen = x->fdp_outlen;
    char *outbuf = x->fdp_outbuf;
    
    for (i = 0 ; i < len ; i++)
    {
        char c = inbuf[i];
        
        if((c != '\n') || (!x->fdp_gotsemi))
            outbuf[outlen++] = c;
        x->fdp_gotsemi = 0; 
        if (outlen >= (BUFSIZE-1)) /*output buffer overflow; reserve 1 for '\n' */
        {
            fprintf(stderr, "pdreceive: message too long; discarding\n");
            outlen = 0;
            x->fdp_discard = 1;
        }  
            /* search for a semicolon.   */
        if (c == ';')
        {
            /* outbuf[outlen++] = '\n'; */
            outbuf[outlen++] = '\0';  /* null terminated for atoi */
            if (!x->fdp_discard)
            {
                pj->snd.a = atoi(outbuf);
                /* debug */
                /* 
                outbuf[outlen] = '\n';
                write(1, outbuf, outlen);
                */
            } /* if (!x->fdp_discard) */

            outlen = 0;
            x->fdp_discard = 0;
            x->fdp_gotsemi = 1;
        } /* if (c == ';') */
    } /* for */

    x->fdp_outlen = outlen;
    return (0);
}

static void tcpread(t_fdpoll *x, PJContext *pj)
{
    int  ret;
    char inbuf[BUFSIZE];

    ret = recv(x->fdp_fd, inbuf, BUFSIZE, 0);
    if (ret < 0)
    {
        sockerror("recv (tcp)");
        rmport(x);
    }
    else if (ret == 0)
        rmport(x);
    else tcpmakeoutput(x, inbuf, ret, pj);
}

static void sockerror(char *s)
{
    int err = errno;
    fprintf(stderr, "%s: %s (%d)\n", s, strerror(err), err);
}

static void x_closesocket(int fd)
{
    close(fd);
}

static void dopoll(PJContext *pj)
{
    int i;
    t_fdpoll *fp;
    fd_set readset, writeset, exceptset;
    FD_ZERO(&writeset);
    FD_ZERO(&readset);
    FD_ZERO(&exceptset);

    FD_SET(sockfd, &readset);
    for (fp = fdpoll, i = nfdpoll; i--; fp++)
        FD_SET(fp->fdp_fd, &readset);
    if (select(maxfd+1, &readset, &writeset, &exceptset, 0) < 0)
    {
        perror("select");
        exit(1);
    }
    for (i = 0; i < nfdpoll; i++)
        if (FD_ISSET(fdpoll[i].fdp_fd, &readset))
            tcpread(&fdpoll[i], pj);
    if (FD_ISSET(sockfd, &readset))
        doconnect();
}
/* PDRCV */

#define PJDebug(pj, printf_arg) ((pj)->verbose.debug ? (printf printf_arg) : 0)


static double GetCurrentTimeInMilliSecond(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static int GetLastFileModifyTime(const char *file_path, time_t *out_mod_time)
{
    struct stat sb;
    if (stat(file_path, &sb) == -1) {
        return 1;
    }
    *out_mod_time = sb.st_mtime;
    return 0;
}

/* SourceObject */
static SourceObject *SourceObject_Create(const char *path)
{
    SourceObject *so;
    so = malloc(sizeof(*so));
    so->path = path;
    so->last_modify_time = 0;
    return so;
}

static void SourceObject_Delete(void *so)
{
    free(so);
}

/* PJContext */
int PJContext_Construct(PJContext *pj)
{
    int scaling_numer, scaling_denom;
    scaling_numer = 1;
    scaling_denom = 2;
    pj->graphics = Graphics_Create(Graphics_LAYOUT_RIGHT_TOP,
                                   scaling_numer, scaling_denom);
    if (!pj->graphics) {
        fprintf(stderr, "Graphics Initialize failed:\r\n");
        fprintf(stderr, " maybe GPU memory allocation failed\r\n");
        fprintf(stderr, " modify /boot/config.txt\r\n");
        fprintf(stderr, "  'gpu_mem_256' or 'gpu_mem_512' assign to more than today:)\r\n");
        fprintf(stderr, " see: http://elinux.org/RPiconfig#Memory\r\n");
        return 2;
    }

    pj->layout_backup = Graphics_LAYOUT_FULLSCREEN;
    pj->is_fullscreen = 0;
    pj->use_backbuffer = 0;
    pj->mouse.x = 0;
    pj->mouse.y = 0;
    pj->mouse.fd = open(MOUSE_DEVICE_PATH, O_RDONLY | O_NONBLOCK);
    /* PDRCV GND */
    pj->snd.a = 0;
    /* */
    pj->time_origin = GetCurrentTimeInMilliSecond();
    pj->frame = 0;
    pj->verbose.render_time = 0;
    pj->verbose.debug = 0;
    pj->scaling.numer = scaling_numer;
    pj->scaling.denom = scaling_denom;
    /* PDRCV GND */
    PJContext_Listen();
    /* */
    return 0;
}

void PJContext_Listen()
{
    int portno = 6666 ;
    struct sockaddr_in server;
    int nretry = 10;
    protocol = SOCK_STREAM;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        sockerror("socket()");
        exit(1);
    }
    maxfd = sockfd + 1;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;

        /* assign client port number */
    server.sin_port = htons((unsigned short)portno);

        /* name the socket */
    if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        sockerror("bind");
        x_closesocket(sockfd);
        return (0);
    }
    if (listen(sockfd, 5) < 0)
    {
         sockerror("listen");
         x_closesocket(sockfd);
         exit(1);
    }
}

void PJContext_Destruct(PJContext *pj)
{
    int i;
    RenderLayer *layer;

    if (pj->mouse.fd >= 0) {
        close(pj->mouse.fd);
    }
    for (i = 0; (layer = Graphics_GetRenderLayer(pj->graphics, i)) != NULL; i++) {
        SourceObject_Delete(RenderLayer_GetAux(layer));
    }
    Graphics_Delete(pj->graphics);
}

static int PJContext_ChangeLayout(PJContext *pj, Graphics_LAYOUT layout)
{
    Graphics_SetLayout(pj->graphics, layout);
    Graphics_ApplyLayoutChange(pj->graphics);

    {
        int width, height;
        Graphics_GetWindowSize(pj->graphics, &width, &height);
        printf("size = %dx%d px\r\n", width, height);
    }
    return 0;
}

static int PJContext_Relayout(PJContext *pj, int forward)
{
    Graphics_LAYOUT layout;
    pj->is_fullscreen = 0;
    layout = Graphics_GetLayout(Graphics_GetCurrentLayout(pj->graphics), forward);
    return PJContext_ChangeLayout(pj, layout);
}

static int PJContext_NextLayout(PJContext *pj)
{
    return PJContext_Relayout(pj, 1);
}

static int PJContext_PreviousLayout(PJContext *pj)
{
    return PJContext_Relayout(pj, 0);
}

static int PJContext_SwitchFullscreen(PJContext *pj)
{
    if (pj->is_fullscreen) {
        pj->is_fullscreen = 0;
        return PJContext_ChangeLayout(pj, pj->layout_backup);
    } else {
        pj->is_fullscreen = 1;
        pj->layout_backup = Graphics_GetCurrentLayout(pj->graphics);
        return PJContext_ChangeLayout(pj, Graphics_LAYOUT_FULLSCREEN);
    }
}

static int PJContext_SwitchBackbuffer(PJContext *pj)
{
    pj->use_backbuffer ^= 1;
    Graphics_SetBackbuffer(pj->graphics, pj->use_backbuffer);
    return Graphics_ApplyOffscreenChange(pj->graphics);
}

static int PJContext_ChangeScaling(PJContext *pj, int add)
{
    pj->scaling.denom += add;
    if (pj->scaling.denom <= 0) {
        pj->scaling.denom = 1;
    }
    if (pj->scaling.denom >= 16) {
        pj->scaling.denom = 16;
    }
    Graphics_SetWindowScaling(pj->graphics, pj->scaling.numer, pj->scaling.denom);
    Graphics_ApplyWindowScalingChange(pj->graphics);
    {
        int width, height;
        Graphics_GetSourceSize(pj->graphics, &width, &height);
        printf("offscreen size = %dx%d px, scaling = %d/%d\r\n",
               width, height, pj->scaling.numer, pj->scaling.denom);
    }
    return 0;
}

static int PJContext_ReloadAndRebuildShadersIfNeed(PJContext *pj)
{
    int i;
    RenderLayer *layer;

    /* PJDebug(pj, ("PJContext_ReloadAndRebuildShadersIfNeed\r\n")); */
    for (i = 0; (layer = Graphics_GetRenderLayer(pj->graphics, i)) != NULL; i++) {
        time_t t;
        SourceObject *so;
        so = RenderLayer_GetAux(layer);
        if (GetLastFileModifyTime(so->path, &t)) {
            fprintf(stderr, "file open failed: %s\r\n", so->path);
            continue;
        }
        if (so->last_modify_time != t) {
            FILE *fp;
            fp = fopen(so->path, "r");
            if (fp == NULL) {
                fprintf(stderr, "file open failed: %s\r\n", so->path);
            } else {
                size_t len;
                char code[MAX_SOURCE_BUF]; /* hmm.. */
                errno = 0;
                len = fread(code, 1, sizeof(code), fp);
                /* TODO: handle errno */
                if (ferror(fp) != 0) {
                    PJDebug(pj, ("ferror = %d\r\n", ferror(fp)));
                }
                fclose(fp);
                if (errno != 0) {
                    PJDebug(pj, ("errno = %d\r\n", errno));
                }
                PJDebug(pj, ("update: %s\r\n", so->path));
                RenderLayer_UpdateShaderSource(layer, code, (int)len);
                so->last_modify_time = t;
                Graphics_BuildRenderLayer(pj->graphics, i);
            }
        }
    }
    return 0;
}

/* DUMMY FOR NOW .. */ 
static void PJContext_UpdateSocket(PJContext *pj)
{

}

static void PJContext_UpdateMousePosition(PJContext *pj)
{
    int i;
    const int max_zap_event = 16;

    if (pj->mouse.fd < 0) {
        return;
    }

    for (i = 0; i < max_zap_event; i++) {
        struct input_event ev;
        ssize_t len;
        int err;
        errno = 0;
	len = read(pj->mouse.fd, &ev, sizeof(ev));
	err = errno;
        errno = 0;
	if (len != sizeof(ev)) {
            /* no more data */
            break;
        }
        if (err != 0) {
            if (err == EWOULDBLOCK || err == EAGAIN) {
                /* ok... try again next time */
                break;
            } else {
                printf("error on mouse-read: code %d(%s)\r\n", err, strerror(err));
                return;
            }
        }
        if (ev.type == EV_REL) { /* relative-move event */
            switch (ev.code) {
            case REL_X:
                pj->mouse.x += (int)ev.value;
                break;
            case REL_Y:
                pj->mouse.y += -(int)ev.value;
                break;
            default:
                break;
            }
        }
    }

    {
        int width, height;
        /* fix mouse position */
        Graphics_GetWindowSize(pj->graphics, &width, &height);
	pj->mouse.x = CLAMP(0, pj->mouse.x, width);
        pj->mouse.y = CLAMP(0, pj->mouse.y, height);
    }
}

static void PJContext_SetUniforms(PJContext *pj)
{
    double t;
    double mouse_x, mouse_y;
    double snd_a;
    int width, height;

    t = GetCurrentTimeInMilliSecond() - pj->time_origin;

    snd_a = (double)pj->snd.a;

    Graphics_GetWindowSize(pj->graphics, &width, &height);
    mouse_x = (double)pj->mouse.x / width;
    mouse_y = (double)pj->mouse.y / height;

    Graphics_SetUniforms(pj->graphics, t / 1000.0, snd_a,
                         mouse_x, mouse_y, drand48());
}

static void PJContext_Render(PJContext *pj)
{
    if (pj->verbose.render_time) {
        double t, ms;
        t = GetCurrentTimeInMilliSecond();
        Graphics_Render(pj->graphics);
        ms = GetCurrentTimeInMilliSecond() - t;
        printf("render time: %.1f ms (%.0f fps)    \r", ms, 1000.0 / ms);
    } else {
        Graphics_Render(pj->graphics);
    }
}

static void PJContext_AdvanceFrame(PJContext *pj)
{
    pj->frame += 1;
}

static int PJContext_Update(PJContext *pj)
{
    if (PJContext_ReloadAndRebuildShadersIfNeed(pj)) {
        return 1;
    }
    dopoll(pj);
    PJContext_UpdateMousePosition(pj);
    PJContext_SetUniforms(pj);
    PJContext_Render(pj);
    PJContext_AdvanceFrame(pj);
    return 0;
}

static void PrintHelp(void)
{
    printf("Key:\r\n");
    printf("  t        FPS printing\r\n");
    printf("  f        switch to fullscreen mode\r\n");
    printf("  < or >   layout change\r\n");
    printf("  [ or ]   offscreen scaling\r\n");
    printf("  b        backbuffer ON/OFF\r\n");
    printf("  q        exit\r\n");
}

#if 0
static int PJContext_HandleKeyboadEvent(PJContext *pj)
{
    /* TODO */
    return 0;
}
#endif

static int PJContext_PrepareMainLoop(PJContext *pj)
{
    return Graphics_AllocateOffscreen(pj->graphics);    
}

static void PJContext_MainLoop(PJContext *pj)
{
    /* start in fullscreen mode */
    PJContext_SwitchFullscreen(pj); 
    for (;;) {
        switch (getchar()) {
        case 'Q':
        case 'q':
        case VEOF:      /* Ctrl+d */
        case VINTR:     /* Ctrl+c */
        case 0x7f:      /* Ctrl+c */
        case 0x03:      /* Ctrl+c */
        case 0x1b:      /* ESC */
            printf("\r\nexit\r\n");
            goto goal;
        case 'f':
        case 'F':
            if (PJContext_SwitchFullscreen(pj)) {
                printf("error\r\n");
                goto goal;
            }
            break;
        case '>':
            if (PJContext_NextLayout(pj)) {
                printf("error\r\n");
                goto goal;
            }
            break;
        case '<':
            if (PJContext_PreviousLayout(pj)) {
                printf("error\r\n");
                goto goal;
            }
            break;
        case ']':
            PJContext_ChangeScaling(pj, 1);
            break;
        case '[':
            PJContext_ChangeScaling(pj, -1);
            break;
        case 't':
        case 'T':
            pj->verbose.render_time ^= 1;
            printf("\r\n");
            break;
        case 'b':
            PJContext_SwitchBackbuffer(pj);
            printf("backbuffer %s\r\n", pj->use_backbuffer ? "ON": "OFF");
            break;
        case '?':
            PrintHelp();
        default:
            break;
        }
        if (PJContext_Update(pj)) {
            break;
        }
    }
  goal:
    return;
}

static int PJContext_AppendLayer(PJContext *pj, const char *path)
{
    SourceObject *so;
    FILE *fp;
    char code[MAX_SOURCE_BUF];
    size_t len;
    PJDebug(pj, ("PJContext_AppendLayer: %s\r\n", path));
    fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "file open failed: %s\r\n", path);
        return 1;
    }
    len = fread(code, 1, sizeof(code), fp);
    fclose(fp);
    so = SourceObject_Create(path);
    Graphics_AppendRenderLayer(pj->graphics, code, (int)len, (void *)so);
    return 0;
}

int PJContext_ParseArgs(PJContext *pj, int argc, const char *argv[])
{
    int i;
    int layer;
    Graphics *g;

    g = pj->graphics;
    layer = 0;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--debug") == 0) {
            pj->verbose.debug = 1;
        } else if (strcmp(arg, "--RGB888") == 0) {
            Graphics_SetOffscreenPixelFormat(g, Graphics_PIXELFORMAT_RGB888);
        } else if (strcmp(arg, "--RGBA8888") == 0) {
            Graphics_SetOffscreenPixelFormat(g, Graphics_PIXELFORMAT_RGBA8888);
        } else if (strcmp(arg, "--RGB565") == 0) {
            Graphics_SetOffscreenPixelFormat(g, Graphics_PIXELFORMAT_RGB565);
        } else if (strcmp(arg, "--nearestneighbor") == 0) {
            Graphics_SetOffscreenInterpolationMode(g, Graphics_INTERPOLATION_MODE_NEARESTNEIGHBOR);
        } else if (strcmp(arg, "--bilinear") == 0) {
            Graphics_SetOffscreenInterpolationMode(g, Graphics_INTERPOLATION_MODE_BILINEAR);
        } else if (strcmp(arg, "--wrap-clamp_to_edge") == 0) {
            Graphics_SetOffscreenWrapMode(g, Graphics_WRAP_MODE_CLAMP_TO_EDGE);
        } else if (strcmp(arg, "--wrap-repeat") == 0) {
            Graphics_SetOffscreenWrapMode(g, Graphics_WRAP_MODE_REPEAT);
        } else if (strcmp(arg, "--wrap-mirror_repeat") == 0) {
            Graphics_SetOffscreenWrapMode(g, Graphics_WRAP_MODE_MIRRORED_REPEAT);
        } else if (strcmp(arg, "--backbuffer") == 0) {
            pj->use_backbuffer = 1;
        } else {
            printf("layer %d: %s\r\n", layer, arg);
            PJContext_AppendLayer(pj, arg);
            layer += 1;
        }
    }
    Graphics_SetBackbuffer(g, pj->use_backbuffer);
    Graphics_ApplyOffscreenChange(pj->graphics);
    return (layer == 0) ? 1 : 0;
}

int PJContext_HostInitialize(void)
{
    Graphics_HostInitialize();
    return 0;
}

void PJContext_HostDeinitialize(void)
{
    Graphics_HostDeinitialize();
}

size_t PJContext_InstanceSize(void)
{
    return sizeof(PJContext);
}

int PJContext_Main(PJContext *pj)
{
    PJContext_PrepareMainLoop(pj);
    PJContext_MainLoop(pj);
    return EXIT_SUCCESS;
}

