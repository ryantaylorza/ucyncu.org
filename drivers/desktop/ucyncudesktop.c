#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/soundcard.h>
#include <linux/alsa.h>
#include <linux/slab.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/socket.h>
#include <linux/uaccess.h>
#include <linux/fs.h>       // Required for file_operations and sysfs
#include <linux/device.h>   // Required for device_create and class_create

// --- Configuration ---
#define UCYNCU_DRIVER_DATE __DATE__
#define MAX_DEVICES 16 

// --- Interoperability Module Control ---
#define UCYNCU_ENABLE_LIVEWIRE 1
#define UCYNCU_ENABLE_DANTE 0
#define UCYNCU_ENABLE_AES67 1

// --- Licensing ---
#ifdef UCYNCU_ENABLE_LIVEWIRE
    #define LIVEWIRE_LICENSE_KEY "XXXX-XXXX-XXXX-XXXX"
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ryan S Taylor");
MODULE_DESCRIPTION("UCYNCU Device Driver with Livewire+, Dante, and AES67 Support");
MODULE_VERSION("0.4");

// --- UCYNCU Device Enable/Disable ---
static int ucyncu_device_enabled = 1; // Default: enabled

module_param(ucyncu_device_enabled, int, 0644); // Make it a module parameter
MODULE_PARM_DESC(ucyncu_device_enabled, "Enable or disable the UCYNCU device (1=enabled, 0=disabled)");

// --- Sysfs Interface (Optional) ---
static struct class *ucyncu_class;
static struct device *ucyncu_device;

static ssize_t enable_show(struct device *dev, struct device_attribute *attr, char *buf) {
    return sprintf(buf, "%d\n", ucyncu_device_enabled);
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
    int val;
    if (sscanf(buf, "%d", &val) != 1) {
        return -EINVAL;
    }
    if (val != 0 && val != 1) {
        return -EINVAL;
    }
    ucyncu_device_enabled = val;
    return count;
}

static DEVICE_ATTR(enable, 0644, enable_show, enable_store);

// Declare the ucyncu_pcm device
static struct snd_pcm *ucyncu_pcm;

// Structure to store server information
struct ucyncu_server {
    char ip_address[16];
    int port;
    struct socket *sock;
    int registered;
};

// Structure to store device information
struct ucyncu_device {
    struct ucyncu_server server;
    // ... other device parameters ...
};

// Function to connect to a UCYNCU server
static int connect_to_server(struct ucyncu_server *server) {
    struct sockaddr_in s_addr;
    int ret;

    ret = sock_create_kern(AF_INET, SOCK_STREAM, IPPROTO_TCP, &server->sock);
    if (ret < 0) {
        printk(KERN_ERR "UCYNCU: Error creating socket: %d\n", ret);
        return ret;
    }

    memset(&s_addr, 0, sizeof(s_addr));
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(server->port);
    s_addr.sin_addr.s_addr = in_aton(server->ip_address);

    ret = kernel_connect(server->sock, (struct sockaddr *)&s_addr, sizeof(s_addr), 0);
    if (ret < 0) {
        printk(KERN_ERR "UCYNCU: Error connecting to server %s:%d: %d\n",
               server->ip_address, server->port, ret);
        sock_release(server->sock);
        server->sock = NULL;
        return ret;
    }

    return 0;
}

// Function to send a registration message to the server
static int register_with_server(struct ucyncu_server *server) {
    if (server->registered) {
        printk(KERN_INFO "UCYNCU: Already registered with server %s:%d\n", server->ip_address, server->port);
        return 0;
    }
    char registration_message[256];
    snprintf(registration_message, sizeof(registration_message),
             "{\"type\": \"REGISTER\", \"device_type\": \"DEVICE\", \"ip_address\": \"%s\", \"max_channels\": 32, \"audio_format\": \"WAV\", \"supported_sample_rates\": [48000], \"device_name\": \"UCYNCU-Device\", \"aoip_support\": {\"aes67\": %s, \"dante\": %s, \"livewire\": {\"enabled\": %s, \"license_key\": \"%s\"}}}",
             server->ip_address,
             UCYNCU_ENABLE_AES67 ? "true" : "false",
             UCYNCU_ENABLE_DANTE ? "true" : "false",
             UCYNCU_ENABLE_LIVEWIRE ? "true" : "false",
             UCYNCU_ENABLE_LIVEWIRE ? LIVEWIRE_LICENSE_KEY : "");

    struct kvec iov = { .iov_base = registration_message, .iov_len = strlen(registration_message) };
    struct msghdr msg = { .msg_flags = MSG_NOSIGNAL };
    int bytes_sent;

    bytes_sent = kernel_sendmsg(server->sock, &msg, &iov, 1, strlen(registration_message));
    if (bytes_sent < 0) {
        printk(KERN_ERR "UCYNCU: Error sending registration message: %d\n", bytes_sent);
        return bytes_sent;
    }

    server->registered = 1;
    printk(KERN_INFO "UCYNCU: Registered with server %s:%d\n", server->ip_address, server->port);

    return 0;
}

// --- Function Prototypes ---

#ifdef UCYNCU_ENABLE_LIVEWIRE
    int ucyncu_livewire_init(struct ucyncu_device *device);
    int ucyncu_livewire_process_packet(struct ucyncu_device *device, struct sk_buff *skb);
    int ucyncu_livewire_send_packet(struct ucyncu_device *device, struct sk_buff *skb);
    void ucyncu_livewire_cleanup(struct ucyncu_device *device);
#endif

#ifdef UCYNCU_ENABLE_DANTE
    int ucyncu_dante_init(struct ucyncu_device *device);
    int ucyncu_dante_process_packet(struct ucyncu_device *device, struct sk_buff *skb);
    int ucyncu_dante_send_packet(struct ucyncu_device *device, struct sk_buff *skb);
    void ucyncu_dante_cleanup(struct ucyncu_device *device);
#endif

#ifdef UCYNCU_ENABLE_AES67
    int ucyncu_aes67_init(struct ucyncu_device *device);
    int ucyncu_aes67_process_packet(struct ucyncu_device *device, struct sk_buff *skb);
    int ucyncu_aes67_send_packet(struct ucyncu_device *device, struct sk_buff *skb);
    void ucyncu_aes67_cleanup(struct ucyncu_device *device);
#endif

static int ucyncu_pcm_open(struct snd_pcm_substream *substream) {
    printk(KERN_INFO "UCYNCU: PCM opened\n");

    // Check if the device is enabled
    if (!ucyncu_device_enabled) {
        printk(KERN_INFO "UCYNCU: Device is disabled\n");
        return -ENODEV; // Or another appropriate error code
    }

    struct ucyncu_device *device = kzalloc(sizeof(struct ucyncu_device), GFP_KERNEL);
    if (!device) {
        printk(KERN_ERR "UCYNCU: Failed to allocate memory for device\n");
        return -ENOMEM;
    }

    strcpy(device->server.ip_address, "192.168.1.254");
    device->server.port = 50007;
    device->server.registered = 0;

    if (connect_to_server(&device->server) < 0) {
        kfree(device);
        printk(KERN_ERR "UCYNCU: Failed to connect to server\n");
        return -1;
    }

    substream->private_data = device;

    if (register_with_server(&device->server) < 0) {
        printk(KERN_ERR "UCYNCU: Failed to register with server\n");
    }

    // Initialize interoperability modules
    #ifdef UCYNCU_ENABLE_LIVEWIRE
        if (ucyncu_livewire_init(device) != 0) {
            printk(KERN_ERR "UCYNCU: Livewire+ initialization failed\n");
        }
    #endif

    #ifdef UCYNCU_ENABLE_DANTE
        if (ucyncu_dante_init(device) != 0) {
            printk(KERN_ERR "UCYNCU: Dante initialization failed\n");
        }
    #endif

    #ifdef UCYNCU_ENABLE_AES67
        if (ucyncu_aes67_init(device) != 0) {
            printk(KERN_ERR "UCYNCU: AES67 initialization failed\n");
        }
    #endif

    return 0;
}

static int ucyncu_pcm_close(struct snd_pcm_substream *substream) {
    printk(KERN_INFO "UCYNCU: PCM closed\n");

    struct ucyncu_device *device = (struct ucyncu_device *)substream->private_data;

    if (device) {
        // Cleanup interoperability modules
        #ifdef UCYNCU_ENABLE_LIVEWIRE
            ucyncu_livewire_cleanup(device);
        #endif

        #ifdef UCYNCU_ENABLE_DANTE
            ucyncu_dante_cleanup(device);
        #endif

        #ifdef UCYNCU_ENABLE_AES67
            ucyncu_aes67_cleanup(device);
        #endif

        if (device->server.sock) {
            sock_release(device->server.sock);
            device->server.sock = NULL;
        }

        kfree(device);
    }

    return 0;
}

static int ucyncu_pcm_hw_params(struct snd_pcm_substream *substream,
                                 struct snd_pcm_hw_params *hw_params) {
    int err;
    unsigned int rate = 48000;
    snd_pcm_format_t format = SNDRV_PCM_FORMAT_S32_LE;

    printk(KERN_INFO "UCYNCU: Setting hardware parameters\n");

    err = snd_pcm_hw_params_set_access(substream, hw_params, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        printk(KERN_ERR "UCYNCU: Error setting access type\n");
        return err;
    }

    err = snd_pcm_hw_params_set_format(substream, hw_params, format);
    if (err < 0) {
        printk(KERN_ERR "UCYNCU: Error setting sample format\n");
        return err;
    }

    err = snd_pcm_hw_params_set_rate_near(substream, hw_params, &rate, 0);
    if (err < 0) {
        printk(KERN_ERR "UCYNCU: Error setting sample rate\n");
        return err;
    }

    err = snd_pcm_hw_params_set_channels(substream, hw_params, 32);
    if (err < 0) {
        printk(KERN_ERR "UCYNCU: Error setting channels\n");
        return err;
    }

    return 0;
}

static int ucyncu_pcm_hw_free(struct snd_pcm_substream *substream) {
    printk(KERN_INFO "UCYNCU: Freeing hardware resources\n");
    return 0;
}

static int ucyncu_pcm_prepare(struct snd_pcm_substream *substream) {
    printk(KERN_INFO "UCYNCU: Preparing stream\n");
    return 0;
}

static int ucyncu_pcm_trigger(struct snd_pcm_substream *substream, int cmd) {
    printk(KERN_INFO "UCYNCU: Triggering stream\n");
    return 0;
}

static snd_pcm_uframes_t ucyncu_pcm_pointer(struct snd_pcm_substream *substream) {
    return 0;
}

static int ucyncu_pcm_read(struct snd_pcm_substream *substream, void *buf, snd_pcm_uframes_t size) {
    printk(KERN_INFO "UCYNCU: Reading audio data\n");

    struct ucyncu_device *device = (struct ucyncu_device *)substream->private_data;
    struct msghdr msg;
    struct kvec iov;
    int bytes_received = -1;
    
    if (device && device->server.sock) {
        memset(&msg, 0, sizeof(msg));
        iov.iov_base = buf;
        iov.iov_len = size;
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        bytes_received = kernel_recvmsg(device->server.sock, &msg, &iov, 1, size, MSG_DONTWAIT);
        if (bytes_received < 0) {
            if (bytes_received == -EAGAIN || bytes_received == -EWOULDBLOCK) {
                return 0;
            } else {
                printk(KERN_ERR "UCYNCU: Error receiving data: %d\n", bytes_received);
                return -1;
            }
        }
    }

    return bytes_received;
}

static int ucyncu_pcm_write(struct snd_pcm_substream *substream, const void *buf, snd_pcm_uframes_t size) {
    printk(KERN_INFO "UCYNCU: Writing audio data\n");

    struct ucyncu_device *device = (struct ucyncu_device *)substream->private_data;
    struct msghdr msg;
    struct kvec iov;
    int bytes_sent = -1;

    if (device && device->server.sock) {
        memset(&msg, 0, sizeof(msg));
        iov.iov_base = (void *)buf;
        iov.iov_len = size;
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        msg.msg_flags = 0;

        bytes_sent =
