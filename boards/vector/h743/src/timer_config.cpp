#include <px4_arch/io_timer_hw_description.h>

// TIM3/PB1 is reserved for Vector's PPS output.
constexpr io_timers_t io_timers[MAX_IO_TIMERS] = {
	initIOTimer(Timer::Timer5, DMA{DMA::Index1, DMA::Stream0, DMA::Channel6}),
	initIOTimer(Timer::Timer4, DMA{DMA::Index1, DMA::Stream6, DMA::Channel2}),
	initIOTimer(Timer::Timer15),
};

constexpr timer_io_channels_t timer_io_channels[MAX_TIMER_IO_CHANNELS] = {
	initIOTimerChannel(io_timers, {Timer::Timer5, Timer::Channel1}, {GPIO::PortA, GPIO::Pin0}),
	initIOTimerChannel(io_timers, {Timer::Timer5, Timer::Channel2}, {GPIO::PortA, GPIO::Pin1}),
	initIOTimerChannel(io_timers, {Timer::Timer5, Timer::Channel3}, {GPIO::PortA, GPIO::Pin2}),
	initIOTimerChannel(io_timers, {Timer::Timer5, Timer::Channel4}, {GPIO::PortA, GPIO::Pin3}),
	initIOTimerChannel(io_timers, {Timer::Timer4, Timer::Channel1}, {GPIO::PortD, GPIO::Pin12}),
	initIOTimerChannel(io_timers, {Timer::Timer4, Timer::Channel2}, {GPIO::PortD, GPIO::Pin13}),
	initIOTimerChannel(io_timers, {Timer::Timer4, Timer::Channel3}, {GPIO::PortD, GPIO::Pin14}),
	initIOTimerChannel(io_timers, {Timer::Timer4, Timer::Channel4}, {GPIO::PortD, GPIO::Pin15}),
	initIOTimerChannel(io_timers, {Timer::Timer15, Timer::Channel1}, {GPIO::PortE, GPIO::Pin5}),
	initIOTimerChannel(io_timers, {Timer::Timer15, Timer::Channel2}, {GPIO::PortE, GPIO::Pin6}),
};

constexpr io_timers_channel_mapping_t io_timers_channel_mapping =
	initIOTimerChannelMapping(io_timers, timer_io_channels);
