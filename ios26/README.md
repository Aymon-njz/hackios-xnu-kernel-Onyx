# Onyx XNU 12377 bootstrap

This is a build-development branch, not a flashable iOS 26 release. It preserves
the working 16.5 branch. bootstrap.patch applies to Apple's pinned
ac9718fb1af618d5ce8678d0dc6e8a58f252216f and includes the local source checkpoint
9842846: generic build prerequisites, Onyx single-CPU Group1 GIC adapter,
staged native UFS interfaces and host checks. No Apple firmware is included.

The workflow attempts actual header generation and kernel compilation using
full Xcode and the Apple-signed KDK 26.5 25F71. A failed link is not a kernel
artifact; logs and any diagnostic objects are retained to resolve failures.
The initial compile target is ARM64 console bring-up. Native ARM64e/PAC,
loader-page storage registration, root mounting and userspace compatibility
still require integration and hardware validation before an iOS 26 boot claim.
