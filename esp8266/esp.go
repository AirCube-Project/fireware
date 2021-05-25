package main

import (
	"device/avr"
	"errors"
	"machine"
	"runtime/interrupt"
	"runtime/volatile"
	"time"
	"unsafe"
        "strconv"
)

const irq = avr.IRQ_SPI_STC

const BUFFER_SIZE = 32

var dataIsReady int
var data [BUFFER_SIZE]byte
var data_length int

// SPIConfig is used to store config info for SPI.
type SPISlaveConfig struct {
	Frequency uint32
	LSBFirst  bool
	Mode      uint8
}

// SPI is for the Serial Peripheral Interface
// Data is taken from http://ww1.microchip.com/downloads/en/DeviceDoc/ATmega48A-PA-88A-PA-168A-PA-328-P-DS-DS40002061A.pdf page 169 and following
type SPISlave struct {
	// The registers for the SPIx port set by the chip
	spcr *volatile.Register8
	spdr *volatile.Register8
	spsr *volatile.Register8

	// The io pins for the SPIx port set by the chip
	sck machine.Pin
	sdi machine.Pin
	sdo machine.Pin
	cs  machine.Pin
}

func (s *SPISlave) handleInterrupt(intr interrupt.Interrupt) {
	result := byte(s.spdr.Get())
//println(strconv.Itoa(int(result)))
	if result == 0xAD {
		for p := 0; p < BUFFER_SIZE; p++ {
			data[p] = buffer[p]
		}
		data_length = ptr
		dataIsReady = 1
		ptr = 0
	} else {
		if ptr < BUFFER_SIZE {
			buffer[ptr] = result
			ptr++
		}
	}
}

// Configure is intended to setup the SPI interface.
func (s SPISlave) Configure(config SPISlaveConfig) error {
	errSPIInvalidMachineConfig := errors.New("SPI port was not configured properly by the machine")
	if s.spcr == (*volatile.Register8)(unsafe.Pointer(uintptr(0))) ||
		s.spsr == (*volatile.Register8)(unsafe.Pointer(uintptr(0))) ||
		s.spdr == (*volatile.Register8)(unsafe.Pointer(uintptr(0))) ||
		s.sck == 0 || s.sdi == 0 || s.sdo == 0 || s.cs == 0 {
		return errSPIInvalidMachineConfig
	}
	if config.Frequency == 0 {
		config.Frequency = 4000000
	}
	s.spcr.Set(0)
	s.spsr.Set(0)
	s.sck.Configure(machine.PinConfig{Mode: machine.PinInput})
	s.sdi.Configure(machine.PinConfig{Mode: machine.PinOutput})
	s.sdo.Configure(machine.PinConfig{Mode: machine.PinInput})
	//	s.cs.High()
	s.cs.Configure(machine.PinConfig{Mode: machine.PinInput})

	frequencyDivider := machine.CPUFrequency() / config.Frequency

	switch {
	case frequencyDivider >= 128:
		s.spcr.SetBits(avr.SPCR_SPR0 | avr.SPCR_SPR1)
	case frequencyDivider >= 64:
		s.spcr.SetBits(avr.SPCR_SPR1)
	case frequencyDivider >= 32:
		s.spcr.SetBits(avr.SPCR_SPR1)
		s.spsr.SetBits(avr.SPSR_SPI2X)
	case frequencyDivider >= 16:
		s.spcr.SetBits(avr.SPCR_SPR0)
	case frequencyDivider >= 8:
		s.spcr.SetBits(avr.SPCR_SPR0)
		s.spsr.SetBits(avr.SPSR_SPI2X)
	case frequencyDivider >= 4:
		// The clock is already set to all 0's.
	default: // defaults to fastest which is /2
		s.spsr.SetBits(avr.SPSR_SPI2X)
	}

	switch config.Mode {
	case machine.Mode1:
		s.spcr.SetBits(avr.SPCR_CPHA)
	case machine.Mode2:
		s.spcr.SetBits(avr.SPCR_CPOL)
	case machine.Mode3:
		s.spcr.SetBits(avr.SPCR_CPHA | avr.SPCR_CPOL)
	default: // default is mode 0
	}

	if config.LSBFirst {
		s.spcr.SetBits(avr.SPCR_DORD)
	}

	// enable SPI, set controller, set clock rate
	s.spcr.SetBits(avr.SPCR_SPE | avr.SPCR_SPIE)

	interrupt.New(irq, SPISlave0.handleInterrupt)
	return nil
}

// Transfer writes the byte into the register and returns the read content
func (s SPISlave) Transfer(b byte) (byte, error) {
	s.spdr.Set(uint8(b))

	for !s.spsr.HasBits(avr.SPSR_SPIF) {
	}

	return byte(s.spdr.Get()), nil
}

var SPISlave0 = SPISlave{
	spcr: avr.SPCR,
	spdr: avr.SPDR,
	spsr: avr.SPSR,
	sck:  machine.PB5,
	sdo:  machine.PB3,
	sdi:  machine.PB4,
	cs:   machine.PB2}

var buffer [BUFFER_SIZE]byte
var ptr int

func onMessage(s string) {
	println(s)
}

type Spot struct {
}

type RGB struct {
	R byte
	G byte
	B byte
}

var spot [7]RGB

func (sp Spot) sendByte(pin machine.Pin, value byte) {
	portSet, maskSet := pin.PortMaskSet()
	portClear, maskClear := pin.PortMaskClear()
	avr.AsmFull(`
           ldi r17, 8			         ; bit counter
     send_bit1:
           st    {portSet}, {maskSet}	   ; set to 1
           lsl   {value}           	   
           brcs  skip_store1              
           st    {portClear}, {maskClear} ; set to 0 (if zero bit)
           nop
           nop
     skip_store1:
           nop                            ; protocol timing adjust
           nop
           nop
           nop
           st    {portClear}, {maskClear} ; end of pulse
           nop                            ; protocol timing adjust
           nop
           nop
           subi  r17, 1                   ; bit loop
           brne  send_bit1                ; send next bit
  `, map[string]interface{}{
		"value":     value,
		"portSet":   portSet,
		"maskSet":   maskSet,
		"portClear": portClear,
		"maskClear": maskClear,
	})
}

func (sp Spot) init(pin machine.Pin) {
	i := 0
	for i < len(spot) {
		spot[i] = RGB{
			R: 0,
			G: 0,
			B: 0,
		}
		i++
	}
	sp.update(pin)
}

//установить точку
func (sp Spot) set(num byte, c RGB) {
	spot[num] = c
}

//получить цвет точки
func (sp Spot) get(num byte) RGB {
	return spot[num]
}

//обновить светодиодную панель
func (sp Spot) update(pin machine.Pin) {
	pin.Low()
	time.Sleep(time.Millisecond * 20)
	i := 0
	for i < len(spot) {
		sp.sendByte(pin, spot[i].G)
		sp.sendByte(pin, spot[i].R)
		sp.sendByte(pin, spot[i].B)
		i++
	}
}

var buttons byte

const BUTTON_LEFT = 1
const BUTTON_CENTER = 2
const BUTTON_RIGHT = 4

func main() {

	ptr = 0
	spi := SPISlave0
	spi.Configure(SPISlaveConfig{Frequency: 250000, LSBFirst: false, Mode: 0})
	println("Initialized")
	println("Mode slave")

	dataIsReady = 0
buttons = 0

    	pin := machine.D3
	pin.Configure(machine.PinConfig{Mode: machine.PinOutput})
        buttonLeft := machine.D4
	buttonLeft.Configure(machine.PinConfig{Mode: machine.PinInput})
        buttonCenter := machine.D5
	buttonCenter.Configure(machine.PinConfig{Mode: machine.PinInput})
        buttonRight := machine.D6
	buttonRight.Configure(machine.PinConfig{Mode: machine.PinInput})

	mx := Spot{}

	mx.init(pin)
	for {
//		for i := 2; i < 31; i++ {
//			for j := 0; j < 7; j++ {
//				mx.set(byte(j), RGB{R: byte(i), G: byte(i), B: byte(i)})
//			}
//			mx.update(pin)
			time.Sleep(time.Microsecond*50)
//check buttons
var newButtons byte
newButtons = 0
if buttonLeft.Get() {
  newButtons |= BUTTON_LEFT
}
if buttonCenter.Get() {
  newButtons |= BUTTON_CENTER
}
if buttonRight.Get() {
  newButtons |= BUTTON_RIGHT
}
if newButtons!=buttons {
//  println(strconv.Itoa(int(newButtons)))
  buttons = newButtons
}

			if dataIsReady != 0 {
println("Data is ready");
				dataIsReady = 0
for i:=0;i<data_length;i++ {
  println(strconv.Itoa(int(data[i])))
}

if data[0]==1 {
  r := data[1]
  g := data[2]
  b := data[3]
  for j:=0;j<7;j++ {
    mx.set(byte(j), RGB{R: r, G: g, B: b})
  }
  mx.update(pin)
//				onMessage(string(data[:data_length]))
}
			}
//		}
//		for i := 31; i > 2; i-- {
//			for j := 0; j < 7; j++ {
//				mx.set(byte(j), RGB{R: byte(i), G: byte(i), B: byte(i)})
//			}
//			mx.update(pin)
//			time.Sleep(time.Millisecond * 100)
//			if dataIsReady != 0 {
//				dataIsReady = 0
//				onMessage(string(data[:data_length]))
//			}
//		}
	}
}
