// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

package astl

import (
	"errors"
	"fmt"
	"reflect"
	"testing"
)

func TestEmptyStatusNames(t *testing.T) {
	for status, name := range map[Status]string{
		StatusNoDataCollected: "NO_DATA_COLLECTED",
		StatusNoNewSample:     "NO_NEW_SAMPLE",
		StatusSampleDropped:   "SAMPLE_DROPPED",
	} {
		if got := StatusName(status); got != name {
			t.Errorf("status %d: got %q, want %q", int(status), got, name)
		}
	}
}

func TestMetricSamplesZeroCountPreservesStatuses(t *testing.T) {
	for _, status := range []Status{StatusNoDataCollected, StatusNoNewSample, StatusSampleDropped} {
		t.Run(StatusName(status), func(t *testing.T) {
			reads := 0
			samples, err := getMetricSamples(
				func() (uint32, error) { return 0, nil },
				func(capacity uint32) ([]Sample, error) {
					reads++
					if capacity != 1 {
						t.Fatalf("capacity = %d, want 1", capacity)
					}
					return nil, Error{Op: "astlGetMetricSamplesOnTarget", Status: status}
				},
			)
			var outcome Error
			if !errors.As(err, &outcome) || outcome.Status != status || outcome.Op != "astlGetMetricSamplesOnTarget" {
				t.Fatalf("status not preserved: %v", err)
			}
			if len(samples) != 0 || reads != 1 {
				t.Fatalf("samples=%v reads=%d", samples, reads)
			}
		})
	}
}

func TestMetricSamplesCountGetRaceAndCapacity(t *testing.T) {
	for _, count := range []uint32{0, 1, 3} {
		want := []Sample{{Timestamp: 123, Value: uint64(42)}}
		got, err := getMetricSamples(func() (uint32, error) { return count, nil },
			func(capacity uint32) ([]Sample, error) {
				expected := count
				if expected == 0 {
					expected = 1
				}
				if capacity != expected {
					t.Fatalf("capacity=%d, want %d", capacity, expected)
				}
				return want, nil // A sample can arrive after a zero count.
			})
		if err != nil || !reflect.DeepEqual(got, want) {
			t.Fatalf("got %v, %v", got, err)
		}
	}
}

func TestMetricSamplesPreservesFailures(t *testing.T) {
	failure := Error{Op: "count", Status: StatusInvalidMetricHandle}
	_, err := getMetricSamples(func() (uint32, error) { return 0, failure },
		func(uint32) ([]Sample, error) { t.Fatal("retrieved after count failed"); return nil, nil })
	if err != failure {
		t.Fatalf("count error = %v", err)
	}
	for _, status := range []Status{StatusBufferTooSmall, StatusInternalError, StatusOutOfMemory} {
		failure = Error{Op: "get", Status: status}
		_, err = getMetricSamples(func() (uint32, error) { return 0, nil },
			func(uint32) ([]Sample, error) { return nil, failure })
		if err != failure {
			t.Fatalf("retrieval error = %v, want %v", err, failure)
		}
	}
}

// This example uses deterministic responses; an application calls GetMetricSamples
// for each metric instead. Recoverable outcomes never prevent visiting the next metric.
func ExampleGetMetricSamples() {
	statuses := []Status{StatusNoNewSample, StatusSampleDropped, StatusNoDataCollected, StatusSuccess}
	for index, status := range statuses {
		samples, err := getMetricSamples(func() (uint32, error) { return 0, nil },
			func(uint32) ([]Sample, error) {
				if status != StatusSuccess {
					return nil, Error{Op: "astlGetMetricSamplesOnTarget", Status: status}
				}
				return []Sample{{Timestamp: 123, Value: uint64(42)}}, nil
			})
		if err != nil {
			var outcome Error
			if errors.As(err, &outcome) {
				switch outcome.Status {
				case StatusNoNewSample, StatusSampleDropped, StatusNoDataCollected:
					fmt.Printf("metric %d: continue collecting\n", index)
					continue
				}
			}
			fmt.Println("collection failed:", err)
			return
		}
		fmt.Printf("metric %d: %d sample(s)\n", index, len(samples))
	}
	// Output:
	// metric 0: continue collecting
	// metric 1: continue collecting
	// metric 2: continue collecting
	// metric 3: 1 sample(s)
}
