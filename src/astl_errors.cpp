#include <string>
#include <unordered_map>

#include "astl/astl.h"

static const std::unordered_map<astl_status_code, const char*> kStatusMap = {
    {ASTL_STATUS_SUCCESS,                                    "SUCCESS"                                   },
    {ASTL_STATUS_BAD_ARGUMENT,                               "BAD_ARGUMENT"                              },
    {ASTL_STATUS_BAD_CONFIGURATION,                          "BAD_CONFIGURATION"                         },
    {ASTL_STATUS_INVALID_TARGET_HANDLE,                      "INVALID_TARGET_HANDLE"                     },
    {ASTL_STATUS_INVALID_COUNTER_HANDLE,                     "INVALID_COUNTER_HANDLE"                    },
    {ASTL_STATUS_INVALID_METRIC_HANDLE,                      "INVALID_METRIC_HANDLE"                     },
    {ASTL_STATUS_INVALID_METRIC_GROUP_HANDLE,                "INVALID_METRIC_GROUP_HANDLE"               },
    {ASTL_STATUS_NOT_IMPLEMENTED,                            "NOT_IMPLEMENTED"                           },
    {ASTL_STATUS_NOT_SUPPORTED,                              "NOT_SUPPORTED"                             },
    {ASTL_STATUS_DEPRECATED_API,                             "DEPRECATED_API"                            },
    {ASTL_STATUS_NO_TARGETS_FOUND,                           "NO_TARGETS_FOUND"                          },
    {ASTL_STATUS_OLD_TARGET_PROPERTIES_STRUCT_VERSION,       "OLD_TARGET_PROPERTIES_STRUCT_VERSION"      },
    {ASTL_STATUS_NEW_TARGET_PROPERTIES_STRUCT_VERSION,       "NEW_TARGET_PROPERTIES_STRUCT_VERSION"      },
    {ASTL_STATUS_NO_COUNTERS_FOUND,                          "NO_COUNTERS_FOUND"                         },
    {ASTL_STATUS_OLD_COUNTER_PROPERTIES_STRUCT_VERSION,      "OLD_COUNTER_PROPERTIES_STRUCT_VERSION"     },
    {ASTL_STATUS_NEW_COUNTER_PROPERTIES_STRUCT_VERSION,      "NEW_COUNTER_PROPERTIES_STRUCT_VERSION"     },
    {ASTL_STATUS_OLD_COUNTER_SAMPLE_STRUCT_VERSION,          "OLD_COUNTER_SAMPLE_STRUCT_VERSION"         },
    {ASTL_STATUS_NEW_COUNTER_SAMPLE_STRUCT_VERSION,          "NEW_COUNTER_SAMPLE_STRUCT_VERSION"         },
    {ASTL_STATUS_NO_METRICS_FOUND,                           "NO_METRICS_FOUND"                          },
    {ASTL_STATUS_OLD_METRIC_PROPERTIES_STRUCT_VERSION,       "OLD_METRIC_PROPERTIES_STRUCT_VERSION"      },
    {ASTL_STATUS_NEW_METRIC_PROPERTIES_STRUCT_VERSION,       "NEW_METRIC_PROPERTIES_STRUCT_VERSION"      },
    {ASTL_STATUS_OLD_METRIC_SAMPLE_STRUCT_VERSION,           "OLD_METRIC_SAMPLE_STRUCT_VERSION"          },
    {ASTL_STATUS_NEW_METRIC_SAMPLE_STRUCT_VERSION,           "NEW_METRIC_SAMPLE_STRUCT_VERSION"          },
    {ASTL_STATUS_NO_METRIC_GROUPS_FOUND,                     "NO_METRIC_GROUPS_FOUND"                    },
    {ASTL_STATUS_OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION, "OLD_METRIC_GROUP_PROPERTIES_STRUCT_VERSION"},
    {ASTL_STATUS_NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION, "NEW_METRIC_GROUP_PROPERTIES_STRUCT_VERSION"},
    {ASTL_STATUS_OLD_COLLECTION_PARAMETERS_STRUCT_VERSION,   "OLD_COLLECTION_PARAMETERS_STRUCT_VERSION"  },
    {ASTL_STATUS_NEW_COLLECTION_PARAMETERS_STRUCT_VERSION,   "NEW_COLLECTION_PARAMETERS_STRUCT_VERSION"  },
    {ASTL_STATUS_TARGET_PROPERTIES_BUFFER_TOO_SMALL,         "TARGET_PROPERTIES_BUFFER_TOO_SMALL"        },
    {ASTL_STATUS_COUNTER_PROPERTIES_BUFFER_TOO_SMALL,        "COUNTER_PROPERTIES_BUFFER_TOO_SMALL"       },
    {ASTL_STATUS_METRIC_PROPERTIES_BUFFER_TOO_SMALL,         "METRIC_PROPERTIES_BUFFER_TOO_SMALL"        },
    {ASTL_STATUS_METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL,   "METRIC_GROUP_PROPERTIES_BUFFER_TOO_SMALL"  },
    {ASTL_STATUS_COUNTER_SAMPLES_BUFFER_TOO_SMALL,           "COUNTER_SAMPLES_BUFFER_TOO_SMALL"          },
    {ASTL_STATUS_METRIC_SAMPLES_BUFFER_TOO_SMALL,            "METRIC_SAMPLES_BUFFER_TOO_SMALL"           },
    {ASTL_STATUS_SAMPLIMG_INTERVAL_TOO_SMALL,                "SAMPLIMG_INTERVAL_TOO_SMALL"               },
    {ASTL_STATUS_SAMPLING_INTERVAL_TOO_LARGE,                "SAMPLING_INTERVAL_TOO_LARGE"               },
    {ASTL_STATUS_SAMPLING_INTERVAL_IGNORED,                  "SAMPLING_INTERVAL_IGNORED"                 },
    {ASTL_STATUS_INVALID_COLLECTION_MODE,                    "INVALID_COLLECTION_MODE"                   },
    {ASTL_STATUS_INVALID_COLLECTION_OPTIMIZATION,            "INVALID_COLLECTION_OPTIMIZATION"           },
    {ASTL_STATUS_COUNTER_NOT_SUPPORTED_ON_TARGET,            "COUNTER_NOT_SUPPORTED_ON_TARGET"           },
    {ASTL_STATUS_METRIC_NOT_SUPPORTED_ON_TARGET,             "METRIC_NOT_SUPPORTED_ON_TARGET"            },
    {ASTL_STATUS_METRIC_GROUP_NOT_SUPPORTED_ON_TARGET,       "METRIC_GROUP_NOT_SUPPORTED"                },
    {ASTL_STATUS_COLLECTION_NOT_RUNNING,                     "COLLECTION_NOT_RUNNING"                    },
    {ASTL_STATUS_COLLECTION_NOT_STOPPED,                     "COLLECTION_NOT_STOPPED"                    },
    {ASTL_STATUS_COLLECTION_NOT_PAUSED,                      "COLLECTION_NOT_PAUSED"                     },
    {ASTL_STATUS_COLLECTION_ALREADY_RUNNING,                 "COLLECTION_ALREADY_RUNNING"                },
    {ASTL_STATUS_COLLECTION_ALREADY_STOPPED,                 "COLLECTION_ALREADY_STOPPED"                },
    {ASTL_STATUS_COLLECTION_ALREADY_PAUSED,                  "COLLECTION_ALREADY_PAUSED"                 },
    {ASTL_STATUS_NO_DATA_COLLECTED,                          "NO_DATA_COLLECTED"                         },
    {ASTL_STATUS_BUFFER_LARGER_THAN_NEEDED,                  "INFO_BUFFER_LARGER_THAN_NEEDED"            },
    {ASTL_STATUS_FILE_OPEN_FAILED,                           "FILE_OPEN_FAILED"                          },
    {ASTL_STATUS_FILE_ERROR,                                 "FILE_ERROR"                                },
    {ASTL_STATUS_OUT_OF_MEMORY,                              "OUT_OF_MEMORY"                             },

    // Add new status codes here

    {ASTL_STATUS_INTERNAL_ERROR,                             "INTERNAL_ERROR"                            },

    {ASTL_STATUS_UNKNOWN_ERROR,                              "UNKNOWN_ERROR"                             }
};

const char* astlStatusString(astl_status_code status) {
  auto status_entry = kStatusMap.find(status);
  return (status_entry != kStatusMap.end()) ? status_entry->second : "UNKNOWN_ERROR";
}
