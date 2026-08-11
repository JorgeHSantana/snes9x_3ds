#include "copyright.h"


#include "ppu.h"

//------------------------------------------------------------------
// Use this to manage vertical sections for brightness, and
// other stuff that change in-frame so as to minimize
// FLUSH_REDRAW
//------------------------------------------------------------------

// Reset vertical sections with a specific value.
// Call this: 
// 1. Inside S9xReset / S9xSoftReset  
//
void S9xResetVerticalSection(VerticalSections *verticalSections, uint32 currentValue)
{
    verticalSections->CurrentValue = currentValue;
    verticalSections->StartY = IPPU.CurrentLine;
    verticalSections->Count = 0;
}


// Reset all vertical sections with no change in the register value.
// Call this: 
// 1. After rendering to screen inside S9xUpdateScreenHardware 
// 2. Inside S9xStartScreenRefresh.
//
void S9xResetVerticalSection(VerticalSections *verticalSections)
{
    verticalSections->StartY = IPPU.CurrentLine;
    verticalSections->Count = 0;
}


// Commits the final value to the list.
// 
// Call this before rendering the screen inside S9xUpdateScreenHardware.
//
#define VERTICAL_SECTION_MAX 241    // capacity of VerticalSections::Section[]

void S9xCommitVerticalSection(VerticalSections *verticalSections)
{
	// Section[] holds 241 entries but a game changing a register on every
	// scanline (windows via HDMA, brightness toggles into vblank) can
	// produce more changes than that in one frame. Unbounded, the append
	// writes past the array into whatever the linker placed next — the
	// layout-dependent corruption behind the "layers break when the menu
	// code changes" saga. Dropping the excess sections only costs a
	// sub-frame effect on the last scanlines.
	if (IPPU.CurrentLine != verticalSections->StartY
		&& verticalSections->Count < VERTICAL_SECTION_MAX)
	{
		verticalSections->Section[verticalSections->Count].StartY = verticalSections->StartY;
		verticalSections->Section[verticalSections->Count].EndY = IPPU.CurrentLine - 1;
		verticalSections->Section[verticalSections->Count].Value = verticalSections->CurrentValue;
		verticalSections->Count ++;
		verticalSections->StartY = IPPU.CurrentLine;
	}
}

// Sets a new value to this section, and commits it
// if the current scanline is different from the last.
//
// This should be called when the corresponding SNES register
// value is updated. 
//
void S9xUpdateVerticalSectionValue(VerticalSections *verticalSections, uint32 newValue)
{
	if (IPPU.RenderThisFrame && 
		IPPU.CurrentLine != verticalSections->StartY && verticalSections->CurrentValue != newValue
		&& verticalSections->Count < VERTICAL_SECTION_MAX)
	{
		verticalSections->Section[verticalSections->Count].StartY = verticalSections->StartY;
		verticalSections->Section[verticalSections->Count].EndY = IPPU.CurrentLine - 1;
		verticalSections->Section[verticalSections->Count].Value = verticalSections->CurrentValue;
		verticalSections->Count ++;
		verticalSections->StartY = IPPU.CurrentLine;
	}
	verticalSections->CurrentValue = newValue;
}
