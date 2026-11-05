/*

priiloader(preloader mod) - A tool which allows to change the default boot up sequence on the Wii console

Copyright (C) 2013-2019  DacoTaco

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

#ifndef ___IOS_SCAN_H_
#define ___IOS_SCAN_H_

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

//The pattern scan of PatchIOS, with no hardware in it so the host tests can run it.
//PatchT has a std::vector<u8> Pattern and a Patch(u8* address) member; this is IosPatch (IOS.hpp).
//Scans [begin, end) once. At each address the first patch not yet applied whose pattern matches there
//is applied (a pattern is never read past end). Each patch is applied once at most, at its first match,
//and the scan stops when every patch has been applied. A patch that never matches makes the scan read to
//end, but writes nothing more: before, a patch set that can't all match on one IOS (FakeSign and
//FakeSignOld) wrote every later match of the others across all of IOS's memory. A patch with an empty
//pattern is never applied and does not hold the scan up. onApplied(address, patternSize) runs after each
//applied patch. Returns how many patches were applied.
template <class PatchT, class AppliedFn>
static inline uint32_t ScanAndApplyPatches(uint8_t* begin, uint8_t* end, const std::vector<PatchT>& patches, AppliedFn onApplied)
{
	uint32_t applied = 0;
	if (begin == NULL || end <= begin || patches.empty())
		return 0;

	std::vector<bool> found(patches.size(), false);
	size_t missing = patches.size();
	for (size_t i = 0; i < patches.size(); i++)
	{
		if (patches[i].Pattern.empty())
		{
			found[i] = true;
			missing--;
		}
	}

	if (missing == 0)
		return 0;

	for (uint8_t* address = begin; address < end; address++)
	{
		size_t left = (size_t)(end - address);
		for (size_t i = 0; i < patches.size(); i++)
		{
			const size_t size = patches[i].Pattern.size();
			if (found[i] || size > left || memcmp(address, &patches[i].Pattern[0], size) != 0)
				continue;

			patches[i].Patch(address);
			onApplied(address, size);
			applied++;
			found[i] = true;
			missing--;
			break;
		}

		if (missing == 0)
			break;
	}

	return applied;
}

#endif
