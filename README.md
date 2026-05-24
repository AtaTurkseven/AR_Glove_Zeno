# AR_Glove_Zeno

A wearable gesture-control prototype for Zeno AR glasses.

AR_Glove_Zeno uses an **MPU6050 mounted on the index finger** to detect hand/finger motion, sends motion data wirelessly using **ESP-NOW**, and displays an animated radial menu on an **OLED HUD receiver**.

The goal is not to build a fake keyboard. The goal is to create a fast AR-style controller for actions like:

- Capture
- Ask Zeno
- Back
- Close
- Select
- Navigate

This project is an early input system for a future Zeno AR glasses interface.

---

## Project Idea

Normal text input from a glove would be slow and painful, like old phone typing.

Instead, this glove acts as a **gesture-based action controller**:

```txt
double circle gesture → open menu
tilt hand/finger      → select option
dip gesture           → confirm action
OLED receiver         → shows animated menu
Serial output         → sends action events for Zeno
