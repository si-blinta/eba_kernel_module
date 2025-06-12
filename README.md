# Exposed Buffer Architecture Kernel Module

This Linux kernel module is a **simple version** of the Exposed Buffer Architecture (EBA) as described by Micah Beck in his research. It is intended as a basic prototype to illustrate some of the core ideas behind EBA, but it is not a full or exact implementation.

## About

The Exposed Buffer Architecture (EBA) is a model that exposes low-level buffer operations—such as allocation, storage, transfer, and transformation—to enable more flexible and programmable networking and computation.

## How to Use

**Configure the Network interface**
    modify INTERFACE_NAME macro in include/ebp.h file.

1. **Build the module:**
    ```
    make
    ```
2. **Insert the module:**
    ```
    sudo make load
    ```
3. **Remove the module:**
    ```
    sudo make remove
    ```
4. **Enable / Disable debug mode :**
    ```
    make debug-on
    ```
    ```
    make debug-off
    ```
5. **Build user Lib :**
    ```
    make lib
    ```

## Notes & Disclaimer

- This project is a **simple version** inspired by Micah Beck’s Exposed Buffer Architecture.
- It may contain bugs, incomplete features, or unexpected behavior.
- **No guarantees** are made regarding stability, security, or production readiness.
- Use, modify, or extend at your own risk.

## References

- Micah Beck, ["Exposed Buffer Architecture"](https://arxiv.org/abs/2209.03488)
- Micah Beck & Terry Moore, ["Exposed Buffer Architecture for Continuum Convergence"](https://arxiv.org/abs/2008.00989)
- [Exposed Buffer Architecture for Programmable and Stateful Networking](https://web.eecs.utk.edu/~mbeck/Exposed_Buffer_Architecture.pdf)
