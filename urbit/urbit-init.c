/* The guest half of `make urbit` and `make comet`: a PID 1 that boots an Urbit
 * ship on a serial console, with the dojo reachable on the host's 8080.
 *
 * There is no distribution here and no package manager.  The initramfs holds
 * this program, one static binary, a bzImage's worth of driver modules and, for
 * a fake ship, a pill.  This program mounts the filesystems the kernel wants,
 * loads the three modules that make a network interface exist at all, points it
 * somewhere, execs vere, and passes the console through.  That is the whole
 * boot, and it is short enough to read in full, which is the point -- see
 * decisions.md item 33.
 *
 * Two modes, chosen at compile time because they are two different images and
 * a ship that quietly changed its mind about what it is would be a bad thing to
 * debug at 03:00:
 *
 *   fake ship  -F zod -B /urbit/pill   offline, reproducible, the same Arvo
 *                                      every time, and item 33's comparison
 *   comet      -c /urbit/ship        a real anonymous ship that mines against
 *                                      a star and syncs, which needs a network,
 *                                      and therefore is not reproducible
 *
 * Everything is pinned and checked.  The kernel, its modules and vere are
 * fetched against recorded sha256s in this file's sibling urbit.lock, because a
 * root filesystem nobody checked is a root filesystem nobody can reason about. */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <linux/route.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

/* Where the images are pinned.  These are the only paths in this program, and
 * they are checked against urbit/urbit.lock by the Makefile before the image is
 * built. */
#define VERE_PATH    "/urbit/vere"
#define PILL_PATH    "/urbit/pill"
#define PIER_PATH    "/pier"
/* A comet's pier lives under /urbit, not at the root.  vere 4.6 resolves -c
 * optarg by realpath()ing its parent and creating only the leaf: realpath of
 * the parent directory has to succeed, and /pier's parent is "" -- realpath("")
 * is ENOENT -- which sends vere down a fallback that does strdup(1) and dies
 * with SIGSEGV before it has printed a prompt.  An existing leaf is refused
 * the other way ("tried to create pier /urbit/ship but it already exists"),
 * so the leaf must not exist and its parent must, and /urbit is the one
 * directory the image guarantees. */
#if URBIT_COMET
#define COMET_PIER_PATH "/urbit/ship"
#endif
#ifndef URBIT_LOOM
#define URBIT_LOOM "31"  /* 31 == 2GB, which is what the 4.x loom wants */
#endif
#define LOOM_EXPONENT URBIT_LOOM

/* 0 == fake ship, 1 == comet.  Set by the Makefile; see the header comment. */
#ifndef URBIT_COMET
#define URBIT_COMET 0
#endif

/* QEMU's user networking.  The guest is 10.0.2.15/24, slirp's gateway is
 * 10.0.2.2 and slirp's resolver is 10.0.2.3.
 *
 * The gateway and the resolver are new, and they exist only because a comet
 * makes outbound connections.  A fake ship never did: hostfwd brought the dojo
 * in and the pill came out of the initramfs, so "no gateway" was correct and is
 * still correct for it.  A comet has to find a star by name, and nothing
 * resolves a name without both of these -- which is what "http: fail (15, 504):
 * temporary failure" and a docket thread giving up with %retry-too-many turned
 * out to mean. */
#define GUEST_ADDR   "10.0.2.15"
#define GUEST_MASK   "255.255.255.0"
#define GUEST_GATEWAY "10.0.2.2"
#define RESOLV_CONF  "/etc/resolv.conf"

#ifndef URBIT_VERBOSE
#define URBIT_VERBOSE 0
#endif

static void say(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0)
        write(STDOUT_FILENO, buf, (size_t)n);
}

/* Write one sysctl, and say so if it did not take: a machine that silently
 * kept the default here is a machine whose loom mapping is about to fail. */
static void write_sysctl(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY);
    ssize_t n;
    int err;

    if (fd < 0) {
        say("urbit-init: could not open %s: %s\n", path, strerror(errno));
        return;
    }
    n = write(fd, value, strlen(value));
    err = errno;
    close(fd);
    if (n != (ssize_t)strlen(value))
        say("urbit-init: could not set %s to %s: %s\n", path, value,
            strerror(err));
}

/* Bring an interface up.  There is no ip(8) here, so this is the ioctl by hand:
 * SIOCGIFFLAGS to read, OR in IFF_UP, SIOCSIFFLAGS to write. */
static int link_up(const char *name)
{
    struct ifreq ifr;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    int rc = -1;

    if (s < 0)
        return -1;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) {
        ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
        rc = ioctl(s, SIOCSIFFLAGS, &ifr);
    }
    close(s);
    return rc;
}

/* Give an interface an address.  QEMU's user networking puts the guest on
 * 10.0.2.15/24.  A fake ship stops there, because hostfwd delivers the dojo
 * inward and the pill came out of the initramfs, so a fake ship never needs to
 * route anything. */
static int link_addr(const char *name, const char *addr, const char *mask)
{
    struct ifreq ifr;
    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    int rc = -1;

    if (s < 0)
        return -1;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, addr, &sin->sin_addr) != 1)
        goto done;
    if (ioctl(s, SIOCSIFADDR, &ifr) != 0)
        goto done;

    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
    sin = (struct sockaddr_in *)&ifr.ifr_netmask;
    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, mask, &sin->sin_addr) == 1)
        rc = ioctl(s, SIOCSIFNETMASK, &ifr);
done:
    close(s);
    return rc;
}

/* A default route, so the guest can start conversations instead of only ending
 * them.  A comet needs this: it dials out to a star.
 *
 * struct rtentry's rt_dev is a char *, not an array, so it needs somewhere to
 * point.  That is the whole reason this function looks the way it does. */
static int link_route(const char *name, const char *gateway)
{
    struct rtentry rt;
    char dev[IFNAMSIZ];
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    int rc = -1;

    if (s < 0)
        return -1;
    memset(&rt, 0, sizeof rt);
    ((struct sockaddr_in *)&rt.rt_gateway)->sin_family = AF_INET;
    ((struct sockaddr_in *)&rt.rt_genmask)->sin_family = AF_INET;
    ((struct sockaddr_in *)&rt.rt_dst)->sin_family = AF_INET;
    if ((size_t)snprintf(dev, sizeof dev, "%s", name) >= sizeof dev)
        goto done;
    rt.rt_dev = dev;
    if (inet_pton(AF_INET, gateway,
                  &((struct sockaddr_in *)&rt.rt_gateway)->sin_addr) != 1)
        goto done;
    /* RTF_GATEWAY is the load-bearing half of this.  Without it the kernel does
     * not read rt_gateway at all: rt_dst is 0.0.0.0, there is no next hop, and
     * the route is refused with ENETUNREACH, which is a strange way to be told
     * "you spelled the route wrong" and looks exactly like "the network is not
     * there".  RTF_UP on its own is a valid flag combination and compiles
     * without complaint, which is what makes it worth writing down. */
    rt.rt_flags = RTF_UP | RTF_GATEWAY;
    rt.rt_metric = 1;                 /* the only route, so it wins */
    if (ioctl(s, SIOCADDRT, &rt) != 0) {
        say("urbit-init: default route via %s refused: %s\n", gateway,
            strerror(errno));
        goto done;
    }
    rc = 0;
done:
    close(s);
    return rc;
}

/* Load one of the three modules the image carries.
 *
 * There is no kmod here, no /sbin/insmod and no shell to run either, so this is
 * init_module(2) by hand.  The modules are in the initramfs rather than in a
 * modloop because Alpine's kernel is PREEMPT_DYNAMIC: the drivers are modules,
 * and the netboot image ships none of them, so without these three the machine
 * has lo and no eth0 and no way to become anything else. */
static int load_module(const char *name)
{
    char path[256];
    static char buf[1 << 20];
    ssize_t n;
    int f;

    if ((size_t)snprintf(path, sizeof path, "/lib/modules/%s", name)
        >= sizeof path) {
        say("urbit-init: module name too long: %s\n", name);
        return -1;
    }
    f = open(path, O_RDONLY);
    if (f < 0) {
        say("urbit-init: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    n = read(f, buf, sizeof buf);
    close(f);
    if (n <= 0) {
        say("urbit-init: cannot read %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (syscall(SYS_init_module, buf, (unsigned long)n, "") != 0) {
        say("urbit-init: cannot load %s: %s\n", name, strerror(errno));
        return -1;
    }
    say("urbit-init: loaded %s (%zd bytes)\n", name, n);
    return 0;
}

/* The driver chain, in the order Alpine's own modules.dep gives for virtio_net:
 * virtio_net wants net_failover, which wants failover.  Loading them out of
 * order fails, and fails in a way that says "invalid module format" rather than
 * "you skipped a dependency", so the order is written down rather than
 * discovered. */
static void load_nic(void)
{
    static const char *const chain[] = {
        "failover.ko", "net_failover.ko", "virtio_net.ko", NULL
    };
    int i, ok = 0;

    for (i = 0; chain[i]; i++)
        if (load_module(chain[i]) == 0)
            ok++;
    if (ok == 0)
        say("urbit-init: no network driver loaded; this ship is offline and "
            "anything that needs the network will not get it\n");
}

/* Point slirp's resolver at the guest.  Written here rather than shipped in
 * the image so that the one number that describes this network lives in one
 * place: the address above. */
static void write_resolv(void)
{
    char line[64];
    int n = snprintf(line, sizeof line, "nameserver %s\n", "10.0.2.3");
    int f;

    if (n <= 0 || (size_t)n >= sizeof line)
        return;
    f = open(RESOLV_CONF, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (f < 0) {
        say("urbit-init: cannot write %s: %s\n", RESOLV_CONF, strerror(errno));
        return;
    }
    if (write(f, line, (size_t)n) != n)
        say("urbit-init: short write to %s: %s\n", RESOLV_CONF, strerror(errno));
    close(f);
}

int main(void)
{
    pid_t child;
    int pipefd[2];
    int status;

    /* /proc and /sys first: the overcommit sysctl below lives in /proc.
     *
     * devtmpfs over /dev is not optional for a Urbit.  A ship breaks symmetry
     * with entropy during its larval stage, and the pinned kernel has no
     * modules, so the only /dev it will ever have is the one it populates
     * itself.  The two hand-written nodes in the cpio are the fallback for a
     * kernel without CONFIG_DEVTMPFS, because /dev/console is how the two say
     * below manage to be said at all. */
    if (mount("proc", "/proc", "proc", 0, NULL) != 0)
        say("urbit-init: mount proc: %s\n", strerror(errno));
    if (mount("sysfs", "/sys", "sysfs", 0, NULL) != 0)
        say("urbit-init: mount sysfs: %s\n", strerror(errno));
    if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) != 0)
        say("urbit-init: mount devtmpfs: %s (continuing with the two nodes "
            "the image has)\n", strerror(errno));
    if (mount("tmpfs", "/tmp", "tmpfs", 0, NULL) != 0)
        say("urbit-init: mount tmpfs on /tmp: %s\n", strerror(errno));

    /* Arvo asks the kernel what it is called.  A fake ship's name is "zod" and
     * it is what the dojo prints.  A comet has no name yet -- it is anonymous
     * until a star tells it what it is -- so the kernel is left alone and the
     * dojo prints whatever mining produced. */
#if !URBIT_COMET
    sethostname("zod", 3);
#endif

    /* vere reserves its whole loom as one mapping before it allocates any of
     * it, and the default overcommit policy answers that request by asking how
     * much memory is free right now.  On a machine with other things running --
     * which is every machine somebody uses this on -- the honest answer is
     * "not that much", and vere stops with "boot: mapping 2048MB failed" and
     * advice about swap.  There is no swap in here to add: the whole root
     * filesystem is an initramfs, so it is gone on reboot by construction.
     *
     * This guest exists to run one process and is thrown away afterwards, so
     * overcommit is set the way a cloud host sets it.  It is a reservation,
     * not an allocation, and the thing that would actually be hurt by a broken
     * promise here is a kernel with an OOM killer and nothing to lose.
     */
    write_sysctl("/proc/sys/vm/overcommit_memory", "1");

    /* lo first: vere binds its http server to loopback before eth0 exists, and
     * a bind to 0.0.0.0 on a machine with no loopback up is a bind that fails
     * later in a confusing place. */
    if (link_up("lo") != 0)
        say("urbit-init: could not bring up lo: %s\n", strerror(errno));

    /* The driver first, then the interface, then the route.  Loading a module
     * is what makes /sys/class/net/eth0 exist, so there is no point addressing
     * an interface that is not there yet -- which is exactly what the old
     * kernel did, failing with ENODEV on a machine that had no eth0 to find. */
    load_nic();
    if (link_addr("eth0", GUEST_ADDR, GUEST_MASK) != 0)
        say("urbit-init: could not address eth0: %s\n", strerror(errno));
    if (link_up("eth0") != 0)
        say("urbit-init: could not bring up eth0: %s\n", strerror(errno));
    /* Outbound only matters to a comet, but it is harmless to a fake ship and
     * having it unconditionally means the image does not have a networking
     * personality that changes silently with the build mode. */
    if (link_route("eth0", GUEST_GATEWAY) != 0)
        say("urbit-init: no default route, so nothing can be dialled out to\n");
    write_resolv();

    /* The dojo reads its commands from stdin, and stdin has to be a pipe
     * because two of them are typed in here rather than by somebody who would
     * have to know them: |mount %base installs the desk and |join %base opens
     * the dojo on it.  A pipe means the dojo would read nobody's typing, so
     * this program becomes a relay -- boot commands down the pipe first, then
     * every line the console produces after that.  Ordering is the pipe's, so
     * the two boot commands are always consumed before any typing, with no
     * race and no sleep. */
    if (pipe(pipefd) != 0) {
        say("urbit-init: pipe: %s\n", strerror(errno));
        return 1;
    }
    child = fork();
    if (child < 0) {
        say("urbit-init: fork: %s\n", strerror(errno));
        return 1;
    }
    if (child == 0) {
        /* The child becomes vere.  It keeps the serial console for output and
         * reads its commands down the pipe, so the console stays a terminal
         * afterwards instead of being consumed by this program. */
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
#if URBIT_COMET
        /* A comet: no -F, no -B, no ship name.  "vere -c <pier>" is the whole
         * of it -- with no key and no pill, vere goes looking for a star, asks
         * to be sponsored, and syncs from whatever it is given.  That is a
         * real ship on the real network, which is the entire difference
         * between this and a fake ship, and it is why this mode needs a
         * network, a name server and hours of CPU that a fake ship does not. */
        execl(VERE_PATH, VERE_PATH,
              "-c", COMET_PIER_PATH,
              "--loom", LOOM_EXPONENT,
              "--http-port", "8080",
              /* stdin is this program's pipe, so vere is right to refuse to
               * set up a terminal on it.  The console it writes to is a real
               * tty, and the dojo's line editing does not need one. */
              "-t",
#if URBIT_VERBOSE
              "-v",
#endif
              NULL);
#else
        execl(VERE_PATH, VERE_PATH,
              "-F", "zod",              /* fake ship: no key, no network */
              "-B", PILL_PATH,          /* Arvo, pinned; see urbit.lock */
              "-c", PIER_PATH,
              "--loom", LOOM_EXPONENT,
              "--http-port", "8080",
              /* stdin is this program's pipe, so vere is right to refuse to
               * set up a terminal on it.  The console it writes to is a real
               * tty, and the dojo's line editing does not need one. */
              "-t",
#if URBIT_VERBOSE
              "-v",
#endif
              NULL);
#endif
        say("urbit-init: cannot run %s: %s\n", VERE_PATH, strerror(errno));
        _exit(127);
    }

    close(pipefd[0]);
#if !URBIT_COMET
    {
        /* |mount %base installs the desk, |join %base opens the dojo on it. */
        static const char boot[] =
            "|mount %base\n"
            "|join %base\n";
        ssize_t n = write(pipefd[1], boot, sizeof boot - 1);
        if (n != (ssize_t)sizeof boot - 1)
            say("urbit-init: short write to the dojo: %s\n", strerror(errno));
    }
#else
    /* A comet gets nothing typed into it.  |mount %base is a thing you do to a
     * desk that exists, and a comet that has just finished mining has none yet
     * -- and a command typed at the wrong moment costs more than a command
     * never typed.  The dojo is on the console for whoever is watching. */
#endif
    /* Deliberately not closed: the dojo is still reading it. */
    fflush(NULL);

    say("urbit-init: vere is pid %d, first boot installs the kernel and "
        "takes a few minutes\n", (int)child);
    say("urbit-init: the dojo is on this console; type Hoon at it, or "
        "Ctrl-A X to quit\n");
    fflush(NULL);

    /* Everything typed at the console from here on is the dojo's.  The console
     * is opened once, for reading: this is the serial line, and it is the only
     * input there is.  Line discipline is left alone on purpose -- in canonical
     * mode a line arrives when Enter is pressed, which is exactly the unit the
     * dojo wants, and the tty echoes the typing back to the same serial line a
     * person is watching. */
    {
        int console = open("/dev/console", O_RDONLY);
        if (console < 0) {
            say("urbit-init: cannot read the console: %s\n", strerror(errno));
        } else {
            char line[1024];
            for (;;) {
                ssize_t n = read(console, line, sizeof line);
                ssize_t off = 0;
                if (n <= 0) {
                    if (n < 0 && errno == EINTR)
                        continue;
                    break;
                }
                while (off < n) {
                    ssize_t w = write(pipefd[1], line + off, (size_t)(n - off));
                    if (w <= 0) {
                        if (w < 0 && errno == EINTR)
                            continue;
                        goto relay_done;
                    }
                    off += w;
                }
            }
        relay_done:
            close(console);
        }
    }
    close(pipefd[1]);

    /* PID 1's job is to keep the machine alive and reap orphans, and a ship
     * that exits should take the machine down with it rather than leave a
     * powered-on guest with nothing running in it. */
    for (;;) {
        pid_t done = wait(&status);
        if (done < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (done == child) {
            if (WIFEXITED(status))
                say("\nurbit-init: vere exited with %d\n", WEXITSTATUS(status));
            else
                say("\nurbit-init: vere was killed by signal %d\n",
                    WTERMSIG(status));
            say("urbit-init: powering off\n");
            fflush(NULL);
            /* glibc's reboot() supplies the magic pair itself, so this takes
             * the command alone.  The raw syscall wants three arguments and
             * silently does nothing with one, which is why an earlier probe
             * printed "powering off" and then sat there. */
            reboot(LINUX_REBOOT_CMD_POWER_OFF);
            /* If the power off did not take, say so instead of hanging with
             * no explanation, which is the failure mode worth avoiding. */
            say("urbit-init: power off did not take; halting\n");
            for (;;)
                pause();
        }
    }
    return 0;
}
