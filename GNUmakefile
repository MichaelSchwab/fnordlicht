include avr.mk

TARGET=fnordlicht
OBJECTS += $(patsubst %.c,%.o,$(shell echo *.c))
HEADERS += $(shell echo *.h)

all: $(TARGET).hex $(TARGET).lst

$(TARGET): $(OBJECTS) $(TARGET).o

%.o: $(HEADERS)

install: program-serial-$(TARGET)

clean: clean-$(TARGET)

clean-$(TARGET):
	$(RM) $(TARGET) $(OBJECTS)
