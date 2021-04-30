#include "structure/PT/pt_insn.hpp"
#include "structure/PT/pt_ild.hpp"
#include "structure/PT/jit_section.hpp"
#include "structure/PT/jit_image.hpp"

#include <intel-pt.h>

int pt_insn_changes_cpl(const struct pt_insn *insn,
			const struct pt_insn_ext *iext)
{
	(void) insn;

	if (!iext)
		return 0;

	switch (iext->iclass) {
	default:
		return 0;

	case PTI_INST_INT:
	case PTI_INST_INT3:
	case PTI_INST_INT1:
	case PTI_INST_INTO:
	case PTI_INST_IRET:
	case PTI_INST_SYSCALL:
	case PTI_INST_SYSENTER:
	case PTI_INST_SYSEXIT:
	case PTI_INST_SYSRET:
		return 1;
	}
}

int pt_insn_changes_cr3(const struct pt_insn *insn,
			const struct pt_insn_ext *iext)
{
	(void) insn;

	if (!iext)
		return 0;

	switch (iext->iclass) {
	default:
		return 0;

	case PTI_INST_MOV_CR3:
		return 1;
	}
}

int pt_insn_is_branch(const struct pt_insn *insn,
		      const struct pt_insn_ext *iext)
{
	(void) iext;

	if (!insn)
		return 0;

	switch (insn->iclass) {
	default:
		return 0;

	case ptic_call:
	case ptic_return:
	case ptic_jump:
	case ptic_cond_jump:
	case ptic_far_call:
	case ptic_far_return:
	case ptic_far_jump:
	case ptic_indirect:
		return 1;
	}
}

int pt_insn_is_far_branch(const struct pt_insn *insn,
			  const struct pt_insn_ext *iext)
{
	(void) iext;

	if (!insn)
		return 0;

	switch (insn->iclass) {
	default:
		return 0;

	case ptic_far_call:
	case ptic_far_return:
	case ptic_far_jump:
		return 1;
	}
}

int pt_insn_binds_to_pip(const struct pt_insn *insn,
			 const struct pt_insn_ext *iext)
{
	if (!iext)
		return 0;

	switch (iext->iclass) {
	default:
		return pt_insn_is_far_branch(insn, iext);

	case PTI_INST_MOV_CR3:
	case PTI_INST_VMLAUNCH:
	case PTI_INST_VMRESUME:
		return 1;
	}
}

int pt_insn_binds_to_vmcs(const struct pt_insn *insn,
			  const struct pt_insn_ext *iext)
{
	if (!iext)
		return 0;

	switch (iext->iclass) {
	default:
		return pt_insn_is_far_branch(insn, iext);

	case PTI_INST_VMPTRLD:
	case PTI_INST_VMLAUNCH:
	case PTI_INST_VMRESUME:
		return 1;
	}
}

int pt_insn_is_ptwrite(const struct pt_insn *insn,
		       const struct pt_insn_ext *iext)
{
	(void) iext;

	if (!insn)
		return 0;

	switch (insn->iclass) {
	default:
		return 0;

	case ptic_ptwrite:
		return 1;
	}
}

int pt_insn_next_ip(uint64_t *pip, const struct pt_insn *insn,
		    const struct pt_insn_ext *iext)
{
	uint64_t ip;

	if (!insn || !iext)
		return -pte_internal;

	ip = insn->ip + insn->size;

	switch (insn->iclass) {
	case ptic_ptwrite:
	case ptic_other:
		break;

	case ptic_call:
	case ptic_jump:
		if (iext->variant.branch.is_direct) {
			ip += (uint64_t) (int64_t)
				iext->variant.branch.displacement;
			break;
		}

	default:
		return -pte_bad_query;

	case ptic_unknown:
		return -pte_bad_insn;
	}

	if (pip)
		*pip = ip;

	return 0;
}

static int pt_insn_decode_retry(struct pt_insn *insn, struct pt_insn_ext *iext,
				struct jit_image *image)
{
	int errcode, isid;
	uint8_t isize, remaining;

	if (!insn)
		return -pte_internal;

	isize = insn->size;
	remaining = sizeof(insn->raw) - isize;

	/* We failed for real if we already read the maximum number of bytes for
	 * an instruction.
	 */
	if (!remaining)
		return -pte_bad_insn;

	/* Read the remaining bytes from the image. */
	jit_section *section = nullptr;
	errcode = jit_image_find(image, &section, insn->ip + isize);
	if (errcode < 0)
		return errcode;

	errcode = jit_section_read(section, insn->raw, sizeof(insn->raw), insn->ip);
	if (errcode <= 0) {
		/* We should have gotten an error if we were not able to read at
		 * least one byte.  Check this to guarantee termination.
		 */
		if (!errcode)
			return -pte_internal;

		/* Preserve the original error if there are no more bytes. */
		if (errcode == -pte_nomap)
			errcode = -pte_bad_insn;

		return errcode;
	}

	/* Add the newly read bytes to the instruction's size. */
	insn->size += (uint8_t) errcode;

	/* Store the new size to avoid infinite recursion in case instruction
	 * decode fails after length decode, which would set @insn->size to the
	 * actual length.
	 */
	errcode = insn->size;

	/* Try to decode the instruction again.
	 *
	 * If we fail again, we recursively retry again until we either fail to
	 * read more bytes or reach the maximum number of bytes for an
	 * instruction.
	 */
	errcode = pt_ild_decode(insn, iext);
	if (errcode < 0) {
		if (errcode != -pte_bad_insn)
			return errcode;

		/* If instruction length decode already determined the size,
		 * there's no point in reading more bytes.
		 */
		if (insn->size != (uint8_t) errcode)
			return errcode;

		return pt_insn_decode_retry(insn, iext, image);
	}

	/* We succeeded this time, so the instruction crosses image section
	 * boundaries.
	 *
	 * This poses the question which isid to use for the instruction.
	 *
	 * To reconstruct exactly this instruction at a later time, we'd need to
	 * store all isids involved together with the number of bytes read for
	 * each isid.  Since @insn already provides the exact bytes for this
	 * instruction, we assume that the isid will be used solely for source
	 * correlation.  In this case, it should refer to the first byte of the
	 * instruction - as it already does.
	 */
	insn->truncated = 1;

	return errcode;
}

int pt_insn_decode(struct pt_insn *insn, struct pt_insn_ext *iext,
		   struct jit_image *image)
{
	int errcode;

	if (!insn)
		return -pte_internal;

	/* Read the memory at the current IP in the current address space. */
	jit_section *section = nullptr;
	errcode = jit_image_find(image, &section, insn->ip);
	if (errcode < 0)
		return errcode;

	errcode = jit_section_read(section, insn->raw, sizeof(insn->raw), insn->ip);
	if (errcode < 0)
		return errcode;
	/* We initialize @insn->size to the maximal possible size.  It will be
	 * set to the actual size during instruction decode.
	 */
	insn->size = (uint8_t) errcode;

	errcode = pt_ild_decode(insn, iext);
	if (errcode < 0) {
		if (errcode != -pte_bad_insn)
			return errcode;

		/* If instruction length decode already determined the size,
		 * there's no point in reading more bytes.
		 */
		if (insn->size != (uint8_t) errcode)
			return errcode;

		return pt_insn_decode_retry(insn, iext, image);
	}

	return errcode;
}

int pt_insn_range_is_contiguous(uint64_t begin, uint64_t end,
				enum pt_exec_mode mode, struct jit_image *image,
				size_t steps)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;

	memset(&insn, 0, sizeof(insn));

	insn.mode = mode;
	insn.ip = begin;

	while (insn.ip != end) {
		int errcode;

		if (!steps--)
			return 0;

		errcode = pt_insn_decode(&insn, &iext, image);
		if (errcode < 0)
			return errcode;

		errcode = pt_insn_next_ip(&insn.ip, &insn, &iext);
		if (errcode < 0)
			return errcode;
	}

	return 1;
}