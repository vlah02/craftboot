/* SBAT metadata. Ubuntu ships shim 15.8, which refuses to load any second-stage
 * image lacking a valid .sbat section (Security Violation) regardless of its
 * signature. Defining the section in a compiled TU lets the LINKER place it at
 * a proper RVA inside SizeOfImage (objcopy --add-section does NOT — it lands
 * outside the mapped image). Format per https://github.com/rhboot/shim/blob/main/SBAT.md */
__attribute__((section(".sbat"), used))
const char craftboot_sbat[] =
    "sbat,1,SBAT Version,sbat,1,https://github.com/rhboot/shim/blob/main/SBAT.md\n"
    "craftboot,1,Craftboot,craftboot,3.0,https://github.com/vlah02/craftboot\n";
