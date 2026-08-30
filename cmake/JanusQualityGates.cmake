function(janus_add_release_quality_tests python source_dir janus_target)
    add_test(
        NAME docs.version_consistency
        COMMAND
            ${CMAKE_COMMAND}
            "-DSOURCE_DIR=${source_dir}"
            -P "${source_dir}/cmake/verify_documentation.cmake"
    )
    add_test(
        NAME docs.public_surface
        COMMAND
            "${python}"
            "${source_dir}/scripts/check_public_surface.py"
            --root "${source_dir}"
            --janus $<TARGET_FILE:${janus_target}>
    )
    add_test(
        NAME docs.stability_inventory_current
        COMMAND
            "${python}"
            "${source_dir}/scripts/check_stability_inventory.py"
            --root "${source_dir}"
    )
    add_test(
        NAME docs.release_gates_1_0
        COMMAND
            "${python}"
            "${source_dir}/scripts/check_release_gates.py"
            --root "${source_dir}"
    )
endfunction()
