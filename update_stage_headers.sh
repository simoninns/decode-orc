#!/bin/bash
# Update stage headers to add ObservationContext parameter

for file in orc/core/stages/**/*_stage.h; do
    if grep -q "std::vector<ArtifactPtr> execute(" "$file"; then
        if ! grep -q "ObservationContext& observation_context" "$file"; then
            echo "Updating $file"
            sed -i '/std::vector<ArtifactPtr> execute(/,/)/ {
                s/) override;$/, ObservationContext\& observation_context) override;/
                /const std::map<std::string, ParameterValue>& parameters$/a\        , ObservationContext\& observation_context
            }' "$file"
        fi
    fi
done

echo "Headers updated"
