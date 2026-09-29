#include <px4_arch/spi_hw_description.h>
#include <drivers/drv_sensor.h>
#include <nuttx/spi/spi.h>

constexpr px4_spi_bus_t px4_spi_buses[SPI_BUS_MAX_BUS_ITEMS] = {
	initSPIBus(SPI::Bus::SPI1, {
		initSPIDevice(DRV_IMU_DEVTYPE_ICM45686, SPI::CS{GPIO::PortE, GPIO::Pin9}),
	}),
	initSPIBus(SPI::Bus::SPI2, {
		initSPIDevice(DRV_OSD_DEVTYPE_ATXXXX, SPI::CS{GPIO::PortD, GPIO::Pin10}),
	}),
};

static constexpr bool unused = validateSPIConfig(px4_spi_buses);
