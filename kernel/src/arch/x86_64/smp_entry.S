/* NOTYVOS AP entry point.
 *
 * Limine jumps here on each application processor with:
 *   rdi = limine_mp_info*  (points at the CPU's MP info struct)
 *
 * limine_mp_info layout (v12, in order):
 *   offset  0: u32 processor_id
 *   offset  4: u32 lapic_id
 *   offset  8: u64 reserved
 *   offset 16: limine_goto_address goto_address  (8 bytes)
 *   offset 24: u64 extra_argument
 *
 * We placed our own stack top in extra_argument. Switch to it, call
 * ap_main, and idle in hlt forever. ap_entry must not return. */

.intel_syntax noprefix
.section .text
.global ap_entry
.type ap_entry, @function
ap_entry:
    cli
    cld

    /* Load our private stack top from extra_argument (offset 24). */
    mov rsp, [rdi + 24]
    and rsp, -16
    xor rbp, rbp

    /* Call ap_main(cpu). */
    call ap_main

.Lap_idle:
    hlt
    jmp .Lap_idle
.size ap_entry, .-ap_entry
