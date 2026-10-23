import { useEffect, useRef } from "react";

import { cn } from "../lib/cn";

const WIDTH = 160;
const HEIGHT = 128;
// Canvas pixels per panel pixel; one of them is the dot-matrix gap.
const SCALE = 4;

const GLASS_CLASS = [
  "relative flex h-full w-full flex-col items-center justify-center gap-[4%] py-[6%]",
  "overflow-hidden rounded-[7%] bg-black",
].join(" ");

const GLARE =
  "linear-gradient(122deg, rgba(255,255,255,0.10) 0%, rgba(255,255,255,0.03) 14%, transparent 32%)";

// fb is GET /screen: 4-bit pixels, two per byte, the low nibble the even x. An OLED's
// unlit pixels are truly black, so only lit ones are drawn.
function drawFrame(ctx, fb) {
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, WIDTH * SCALE, HEIGHT * SCALE);

  for (let y = 0; y < HEIGHT; y++) {
    for (let x = 0; x < WIDTH; x++) {
      const byte = fb ? fb[y * (WIDTH / 2) + (x >> 1)] : 0;
      const level = x & 1 ? byte >> 4 : byte & 15;
      if (!level) {
        continue;
      }

      ctx.fillStyle = `rgba(238,245,255,${0.35 + (0.65 * level) / 15})`;
      ctx.fillRect(x * SCALE, y * SCALE, SCALE - 1, SCALE - 1);
    }
  }
}

// The panel as a live dot matrix in the device's white case, so a preview reads as the
// real product (body 63.6 x 54.6 mm, a thin bezel around the glass). led is the
// indicator colour, "#RRGGBB" or "off".
export function DeviceScreen({ fb, led = "off", className }) {
  const canvas = useRef(null);

  useEffect(() => {
    const ctx = canvas.current?.getContext("2d");
    if (ctx) {
      drawFrame(ctx, fb);
    }
  }, [fb]);

  const lit = led && led !== "off";
  const indicator = lit
    ? { background: led, boxShadow: `0 0 8px ${led}, 0 0 3px ${led}` }
    : { background: "rgba(255,255,255,0.08)" };

  return (
    <div
      className={cn(
        "aspect-[64/55] rounded-[9%] bg-gradient-to-b from-[#fbfbfc] to-[#eaeaee] p-[3%]",
        "shadow-[0_14px_30px_-12px_rgba(0,0,0,0.45)] ring-1 ring-black/[0.08]",
        className,
      )}
    >
      <div className={GLASS_CLASS}>
        <span
          className="h-[3px] w-[24%] shrink-0 rounded-full transition-colors"
          style={indicator}
        />
        <canvas
          ref={canvas}
          width={WIDTH * SCALE}
          height={HEIGHT * SCALE}
          className="block aspect-[5/4] w-[86%]"
        />
        <div className="pointer-events-none absolute inset-0" style={{ background: GLARE }} />
      </div>
    </div>
  );
}
