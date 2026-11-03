import { Box, ButtonBase, Tooltip } from '@mui/material';
import CheckIcon from '@mui/icons-material/Check';
import { PALETTE, type Rgb } from '@/core/bulb';

const same = (a: Rgb, b: Rgb) => a.r === b.r && a.g === b.g && a.b === b.b;

/** One-tap colours. */
export function PaletteGrid({ current, onPick, disabled }: { current?: Rgb; onPick: (c: Rgb) => void; disabled?: boolean }) {
  return (
    <Box sx={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(52px, 1fr))', gap: 1.5 }}>
      {PALETTE.map(({ name, color }) => {
        const selected = !!current && same(current, color);
        const light = color.r * 0.3 + color.g * 0.59 + color.b * 0.11 > 160;
        return (
          <Tooltip key={name} title={name}>
            <ButtonBase
              aria-label={name}
              disabled={disabled}
              onClick={() => onPick(color)}
              sx={{
                aspectRatio: '1', borderRadius: '50%', bgcolor: `rgb(${color.r}, ${color.g}, ${color.b})`,
                border: '2px solid', borderColor: selected ? 'text.primary' : 'divider',
                opacity: disabled ? 0.4 : 1, transition: 'transform 120ms',
                '&:active': { transform: 'scale(0.92)' },
              }}
            >
              {selected && <CheckIcon sx={{ color: light ? '#000' : '#fff' }} />}
            </ButtonBase>
          </Tooltip>
        );
      })}
    </Box>
  );
}
