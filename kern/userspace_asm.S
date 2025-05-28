.global allow_access_user_space
allow_access_user_space:
    li      t0, (1 << 18)
    csrs    sstatus, t0
    jr      ra

.global disallow_access_user_space
disallow_access_user_space:
    li      t0, (1 << 18)
    csrc    sstatus, t0
    jr      ra