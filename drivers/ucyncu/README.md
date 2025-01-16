# UCYNCU Audio Driver

**UCYNCU** is a Linux audio driver designed for reliable and synchronized audio streaming over TCP/IP networks. It aims to provide a robust and flexible solution for professional audio applications, with compatibility for Axia, Livewire+ AES67, and Dante devices.

## Features

* **TCP-Based Transport:** Leverages TCP's reliability and congestion control for stable audio streaming.
* **Centralized Server:**  A UCYNCU server manages connections, routing, and control.
* **ALSA Integration:** Presents itself as an ALSA device for seamless integration with Linux audio applications.
* **Compatibility:** Aims for compatibility with Axia, Livewire+ AES67, and Dante ecosystems.

## Usage

1. **Obtain the UCYNCU Driver:**
    * You can download the pre-compiled driver from the [UCYNCU website](https://ucyncu.com/drivers) or compile it yourself from the source code.

2. **Install the UCYNCU Driver:**
    * Follow the installation instructions provided with the driver. This typically involves copying the driver file to the appropriate directory and loading it using `modprobe`.

3. **Start the UCYNCU Server:**
    Refer to the UCYNCU server documentation for instructions on how to start and configure the server.

4. **Configure Audio Routing:** Use the UCYNCU server interface to configure audio routing between devices and streams.

5. **Use with Applications:**  Once the driver is loaded and routing is configured, you can use ALSA-compatible applications to play and record audio through UCYNCU.

**Additional Notes**

* **Dependencies:** UCYNCU requires a running UCYNCU server to function.
* **Configuration:** For optimal performance, you may need to adjust server settings and driver parameters.
* **Compatibility:** While UCYNCU strives for compatibility, certain configurations or devices may require additional adjustments.

## Contributing

Contributions are welcome! Feel free to submit bug reports, feature requests, or pull requests.

## License

This driver is licensed under the GPL. See the LICENSE file for details.
