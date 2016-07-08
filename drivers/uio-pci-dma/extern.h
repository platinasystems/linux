extern int uio_dma_init_module (void);
extern int uio_pci_init_module (void);

extern void uio_pci_exit_module (void);
extern void uio_dma_exit_module (void);

extern int uio_dma_device_open(struct device *dev, uint32_t id);
extern int uio_dma_device_close(uint32_t id);
