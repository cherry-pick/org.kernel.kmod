#include "org.kernel.kmod.varlink.h"

#include <errno.h>
#include <libkmod.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <varlink.h>

#define _cleanup_(_x) __attribute__((__cleanup__(_x)))

static inline void freep(void *p) {
        free(*(void **)p);
}

static inline void closep(int *fd) {
        if (*fd >= 0)
                close(*fd);
}

static void kmod_unrefp(struct kmod_ctx **kmodp) {
        if (*kmodp)
                kmod_unref(*kmodp);
}

static void kmod_module_unrefp(struct kmod_module **modulep) {
        if (*modulep)
                kmod_module_unref(*modulep);
}

static void kmod_module_unref_listp(struct kmod_list **listp) {
        if (*listp)
                kmod_module_unref_list(*listp);
}

static void kmod_module_info_free_listp(struct kmod_list **listp) {
        if (*listp)
                kmod_module_info_free_list(*listp);
}

struct parm {
        char *name;
        const char *type;
        const char *description;
        struct parm *next;
};

static void parms_freep(struct parm **parmsp) {
        struct parm *parm = *parmsp;

        while (parm) {
                struct parm *it = parm;
                parm = parm->next;
                free(it->name);
                free(it);
        }
}

static long parm_set(struct parm **parms, const char *type, const char *value) {
        const char *colon;
        _cleanup_(freep) char *name = NULL;
        struct parm *parm;

        colon = strchr(value, ':');
        if (!colon)
                return -EINVAL;

        name = strndup(value, colon - value);

        for (parm = *parms; parm; parm = parm->next) {
                if (strcmp(parm->name, name) == 0)
                        break;
        }

        if (!parm) {
                parm = calloc(1, sizeof(struct parm));
                parm->name = name;
                name = NULL;
                parm->next = *parms;
                *parms = parm;
        }

        if (strcmp(type, "parm") == 0) {
                if (parm->description)
                        return -ENOTUNIQ;
                parm->description = colon + 1;
        } else if (strcmp(type, "parmtype") == 0) {
                if (parm->type)
                        return -ENOTUNIQ;
                parm->type = colon + 1;
        }

        return 0;
}

/* Info(module: string) -> (info: ModuleInfo) */
static long org_kernel_kmod_Info(VarlinkService *service,
                                 VarlinkCall *call,
                                 VarlinkObject *parameters,
                                 uint64_t flags,
                                 void *userdata) {
        struct kmod_ctx *kmod = userdata;
        const char *name;
        _cleanup_(kmod_module_unrefp) struct kmod_module *mod = NULL;
        _cleanup_(kmod_module_info_free_listp) struct kmod_list *infos = NULL;
        struct kmod_list *iter;
        const char *description = "";
        const char *author = "";
        const char *license = "";
        const char *version = "";
        const char *srcversion = "";
        const char *vermagic = "";
        const char *depends = "";
        _cleanup_(varlink_array_unrefp) VarlinkArray *aliases = NULL;
        _cleanup_(varlink_array_unrefp) VarlinkArray *params = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *info = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *reply = NULL;
        _cleanup_(parms_freep) struct parm *parms = NULL;
        long r;

        r = varlink_object_get_string(parameters, "module", &name);
        if (r < 0)
                return r;

        r = kmod_module_new_from_name(kmod, name, &mod);
        if (r < 0)
                return varlink_call_reply_error(call, "org.kernel.kmod.UnknownModule", NULL);

        r = kmod_module_get_info(mod, &infos);
        if (r < 0)
                return varlink_call_reply_error(call, "org.kernel.kmod.NoInfoAvailable", NULL);

        if (r < 0)
                return r;

        r = varlink_array_new(&aliases);
        if (r < 0)
                return r;

        kmod_list_foreach(iter, infos) {
                const char *key = kmod_module_info_get_key(iter);
                const char *value = kmod_module_info_get_value(iter);

                if (strcmp(key, "description") == 0)
                        description = value;
                else if (strcmp(key, "author") == 0)
                        author = value;
                else if (strcmp(key, "license") == 0)
                        license = value;
                else if (strcmp(key, "version") == 0)
                        version = value;
                else if (strcmp(key, "srcversion") == 0)
                        srcversion = value;
                else if (strcmp(key, "vermagic") == 0)
                        vermagic = value;
                else if (strcmp(key, "depends") == 0)
                        depends = value;
                else if (strcmp(key, "alias") == 0) {
                        r = varlink_array_append_string(aliases, value);
                        if (r < 0)
                                return r;
                } else if (strcmp(key, "parm") == 0 || strcmp(key, "parmtype") == 0) {
                        r = parm_set(&parms, key, value);
                        if (r < 0)
                                return varlink_call_reply_error(call, "org.kernel.kmod.InvalidParameter", NULL);
                }
        }

        r = varlink_array_new(&params);
        if (r < 0)
                return r;

        for (struct parm *parm = parms; parm; parm = parm->next) {
                _cleanup_(varlink_object_unrefp) VarlinkObject *parameter = NULL;

                if (varlink_object_new(&parameter) < 0 ||
                    varlink_object_set_string(parameter, "name", parm->name))
                        return -EUCLEAN;

                if (parm->type) {
                        r = varlink_object_set_string(parameter, "type", parm->type);
                        if (r < 0)
                                return r;
                }

                if (parm->description) {
                        r = varlink_object_set_string(parameter, "type", parm->description);
                        if (r < 0)
                                return r;
                }

                r = varlink_array_append_object(params, parameter);
                if (r < 0)
                        return r;
        }

        if (varlink_object_new(&info) < 0 ||
            varlink_object_set_string(info, "name", kmod_module_get_name(mod)) < 0 ||
            varlink_object_set_string(info, "description", description) < 0 ||
            varlink_object_set_string(info, "path", kmod_module_get_path(mod)) < 0 ||
            varlink_object_set_string(info, "author", author) < 0 ||
            varlink_object_set_string(info, "license", license) < 0 ||
            varlink_object_set_string(info, "version", version) < 0 ||
            varlink_object_set_string(info, "srcversion", srcversion) < 0 ||
            varlink_object_set_string(info, "vermagic", vermagic) < 0 ||
            varlink_object_set_string(info, "depends", depends) < 0 ||
            varlink_object_set_array(info, "aliases", aliases) < 0 ||
            varlink_object_set_array(info, "parameters", params) < 0)
                return -EUCLEAN;

        if (varlink_object_new(&reply) < 0 || varlink_object_set_object(reply, "info", info) < 0)
                return -EUCLEAN;

        return varlink_call_reply(call, reply, 0);
}

/* List() -> (modules: string[]) */
static long org_kernel_kmod_List(VarlinkService *service,
                                 VarlinkCall *call,
                                 VarlinkObject *parameters,
                                 uint64_t flags,
                                 void *userdata) {
        struct kmod_ctx *kmod = userdata;
        _cleanup_(varlink_array_unrefp) VarlinkArray *modules = NULL;
        _cleanup_(varlink_object_unrefp) VarlinkObject *reply = NULL;
        _cleanup_(kmod_module_unref_listp) struct kmod_list *modules_list = NULL;
        struct kmod_list *modules_iter;
        int r;

        r = varlink_array_new(&modules);
        if (r < 0)
                return r;

        r = kmod_module_new_from_loaded(kmod, &modules_list);
        if (r < 0)
                return r;

        kmod_list_foreach(modules_iter, modules_list) {
                _cleanup_(kmod_module_unrefp) struct kmod_module *mod = NULL;
                _cleanup_(kmod_module_unref_listp) struct kmod_list *holders_list = NULL;
                struct kmod_list *holders_iter = NULL;
                _cleanup_(varlink_object_unrefp) VarlinkObject *module = NULL;
                _cleanup_(varlink_array_unrefp) VarlinkArray *used_by = NULL;

                mod = kmod_module_get_module(modules_iter);

                r = varlink_array_new(&used_by);
                if (r < 0)
                        return r;

                holders_list = kmod_module_get_holders(mod);
                kmod_list_foreach(holders_iter, holders_list) {
                        _cleanup_(kmod_module_unrefp) struct kmod_module *holder;

                        holder = kmod_module_get_module(holders_iter);

                        r = varlink_array_append_string(used_by, kmod_module_get_name(holder));
                        if (r < 0)
                                return r;
                }

                if (varlink_object_new(&module) < 0 ||
                    varlink_object_set_string(module, "name", kmod_module_get_name(mod)) < 0 ||
                    varlink_object_set_int(module, "size", kmod_module_get_size(mod)) < 0 ||
                    varlink_object_set_int(module, "use_count", kmod_module_get_refcnt(mod)) < 0 ||
                    varlink_object_set_array(module, "used_by", used_by) < 0)
                        return -EUCLEAN;

                r = varlink_array_append_object(modules, module);
                if (r < 0)
                        return r;
        }

        r = varlink_object_new(&reply);
        if (r < 0)
                return r;

        r = varlink_object_set_array(reply, "modules", modules);
        if (r < 0)
                return r;

        return varlink_call_reply(call, reply, 0);
}

int main(int argc, char **argv) {
        _cleanup_(kmod_unrefp) struct kmod_ctx *kmod = NULL;
        _cleanup_(varlink_service_freep) VarlinkService *service = NULL;
        const char *address;
        int fd = -1;
        _cleanup_(closep) int fd_epoll = -1;
        _cleanup_(closep) int fd_signal = -1;
        sigset_t mask;
        struct epoll_event ep = {};
        bool exit = false;
        int r;

        kmod = kmod_new(NULL, NULL);
        if (!kmod)
                return EXIT_FAILURE;

        address = argv[1];
        if (!address) {
                fprintf(stderr, "Error: missing address.\n");

                return EXIT_FAILURE;
        }

        /* An activator passed us our connection. */
        if (read(3, NULL, 0) == 0)
                fd = 3;

        r = varlink_service_new(&service, "Kernel Module Information", "0.1", address, fd);
        if (r < 0)
                return EXIT_FAILURE;

        r = varlink_service_add_interface(service, org_kernel_kmod_varlink,
                                         "List", org_kernel_kmod_List, kmod,
                                         "Info", org_kernel_kmod_Info, kmod,
                                         NULL);
        if (r < 0)
                return r;

        fd_epoll = epoll_create1(EPOLL_CLOEXEC);
        if (fd_epoll < 0)
                return EXIT_FAILURE;

        ep.events = EPOLLIN;
        ep.data.fd = varlink_service_get_fd(service);
        if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, varlink_service_get_fd(service), &ep) < 0)
                return EXIT_FAILURE;

        sigemptyset(&mask);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGINT);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        fd_signal = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (fd_signal < 0)
                return EXIT_FAILURE;

        ep.events = EPOLLIN;
        ep.data.fd = fd_signal;
        if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, fd_signal, &ep) < 0)
                return EXIT_FAILURE;

        while (!exit) {
                int n;
                struct epoll_event event;

                n = epoll_wait(fd_epoll, &event, 1, -1);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;

                        return EXIT_FAILURE;
                }

                if (n == 0)
                        continue;

                if (event.data.fd == varlink_service_get_fd(service)) {
                        r = varlink_service_process_events(service);
                        if (r < 0) {
                                fprintf(stderr, "Control: %s\n", strerror(-r));
                                if (r != -EPIPE)
                                        return EXIT_FAILURE;
                        }
                } else if (event.data.fd == fd_signal) {
                        struct signalfd_siginfo fdsi;
                        long size;

                        size = read(fd_signal, &fdsi, sizeof(struct signalfd_siginfo));
                        if (size != sizeof(struct signalfd_siginfo))
                                continue;

                        switch (fdsi.ssi_signo) {
                                case SIGTERM:
                                case SIGINT:
                                        exit = true;
                                        break;

                                default:
                                        return -EINVAL;
                        }
                }
        }

        return EXIT_SUCCESS;
}
