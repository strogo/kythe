/*
 * Copyright 2018 The Kythe Authors. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Package span implements utilities to resolve byte offsets within a file to
// line and column numbers.
package span // import "kythe.io/kythe/go/util/span"

import (
	"bytes"
	"errors"
	"fmt"
	"log"
	"sort"

	"github.com/sergi/go-diff/diffmatchpatch"

	cpb "kythe.io/kythe/proto/common_go_proto"
	xpb "kythe.io/kythe/proto/xref_go_proto"
)

// InBounds reports whether [start,end) is bounded by the specified [startBoundary,endBoundary) span.
func InBounds(kind xpb.DecorationsRequest_SpanKind, start, end, startBoundary, endBoundary int32) bool {
	switch kind {
	case xpb.DecorationsRequest_WITHIN_SPAN:
		return start >= startBoundary && end <= endBoundary
	case xpb.DecorationsRequest_AROUND_SPAN:
		return start <= startBoundary && end >= endBoundary
	default:
		log.Printf("WARNING: unknown DecorationsRequest_SpanKind: %v", kind)
	}
	return false
}

// Patcher uses a computed diff between two texts to map spans from the original
// text to the new text.
type Patcher struct {
	dmp  *diffmatchpatch.DiffMatchPatch
	diff []diffmatchpatch.Diff
}

// NewPatcher returns a Patcher based on the diff between oldText and newText.
func NewPatcher(oldText, newText []byte) *Patcher {
	dmp := diffmatchpatch.New()
	return &Patcher{dmp, dmp.DiffCleanupEfficiency(dmp.DiffMain(string(oldText), string(newText), true))}
}

// Patch returns the resulting span of mapping the given span from the Patcher's
// constructed oldText to its newText.  If the span no longer exists in newText
// or is invalid, the returned bool will be false.  As a convenience, if p==nil,
// the original span will be returned.
func (p *Patcher) Patch(spanStart, spanEnd int32) (newStart, newEnd int32, exists bool) {
	if spanStart > spanEnd {
		return 0, 0, false
	} else if p == nil {
		return spanStart, spanEnd, true
	}

	var old, new int32
	for _, d := range p.diff {
		l := int32(len(d.Text))
		if old > spanStart {
			return 0, 0, false
		}
		switch d.Type {
		case diffmatchpatch.DiffEqual:
			if old <= spanStart && spanEnd <= old+l {
				newStart = new + (spanStart - old)
				newEnd = new + (spanEnd - old)
				exists = true
				return
			}
			old += l
			new += l
		case diffmatchpatch.DiffDelete:
			old += l
		case diffmatchpatch.DiffInsert:
			new += l
		}
	}

	return 0, 0, false
}

// PatchSpan returns the given Span's byte offsets mapped from the Patcher's
// oldText to its newText using Patcher.Patch.
func (p *Patcher) PatchSpan(span *cpb.Span) (newStart, newEnd int32, exists bool) {
	return p.Patch(span.GetStart().GetByteOffset(), span.GetEnd().GetByteOffset())
}

// Normalizer fixes xref.Locations within a given source text so that each point
// has consistent byte_offset, line_number, and column_offset fields within the
// range of text's length and its line lengths.
type Normalizer struct {
	textLen   int32
	lineLen   []int32
	prefixLen []int32
}

// NewNormalizer returns a Normalizer for Locations within text.
func NewNormalizer(text []byte) *Normalizer {
	lines := bytes.Split(text, lineEnd)
	lineLen := make([]int32, len(lines))
	prefixLen := make([]int32, len(lines))
	for i := 1; i < len(lines); i++ {
		lineLen[i-1] = int32(len(lines[i-1]) + len(lineEnd))
		prefixLen[i] = prefixLen[i-1] + lineLen[i-1]
	}
	lineLen[len(lines)-1] = int32(len(lines[len(lines)-1]) + len(lineEnd))
	return &Normalizer{int32(len(text)), lineLen, prefixLen}
}

// Location returns a normalized location within the Normalizer's text.
// Normalized FILE locations have no start/end points.  Normalized SPAN
// locations have fully populated start/end points clamped in the range [0,
// len(text)).
func (n *Normalizer) Location(loc *xpb.Location) (*xpb.Location, error) {
	nl := &xpb.Location{}
	if loc == nil {
		return nl, nil
	}
	nl.Ticket = loc.Ticket
	nl.Kind = loc.Kind
	if loc.Kind == xpb.Location_FILE {
		return nl, nil
	}

	if loc.Span == nil {
		return nil, errors.New("invalid SPAN: missing span")
	} else if loc.Span.Start == nil {
		return nil, errors.New("invalid SPAN: missing span start point")
	} else if loc.Span.End == nil {
		return nil, errors.New("invalid SPAN: missing span end point")
	}

	nl.Span = n.Span(loc.Span)

	start, end := nl.Span.Start.ByteOffset, nl.Span.End.ByteOffset
	if start > end {
		return nil, fmt.Errorf("invalid SPAN: start (%d) is after end (%d)", start, end)
	}
	return nl, nil
}

// Span returns a Span with its start and end normalized.
func (n *Normalizer) Span(s *cpb.Span) *cpb.Span {
	if s == nil {
		return nil
	}
	return &cpb.Span{
		Start: n.Point(s.Start),
		End:   n.Point(s.End),
	}
}

// SpanOffsets returns a Span based on normalized start and end byte offsets.
func (n *Normalizer) SpanOffsets(start, end int32) *cpb.Span {
	return &cpb.Span{
		Start: n.ByteOffset(start),
		End:   n.ByteOffset(end),
	}
}

var lineEnd = []byte("\n")

// Point returns a normalized point within the Normalizer's text.  A normalized
// point has all of its fields set consistently and clamped within the range
// [0,len(text)).
func (n *Normalizer) Point(p *cpb.Point) *cpb.Point {
	if p == nil {
		return nil
	}

	if p.ByteOffset > 0 {
		return n.ByteOffset(p.ByteOffset)
	} else if p.LineNumber > 0 {
		np := &cpb.Point{
			LineNumber:   p.LineNumber,
			ColumnOffset: p.ColumnOffset,
		}

		if totalLines := int32(len(n.lineLen)); p.LineNumber > totalLines {
			np.LineNumber = totalLines
			np.ColumnOffset = n.lineLen[np.LineNumber-1] - 1
		}
		if np.ColumnOffset < 0 {
			np.ColumnOffset = 0
		} else if np.ColumnOffset > 0 {
			if lineLen := n.lineLen[np.LineNumber-1] - 1; p.ColumnOffset > lineLen {
				np.ColumnOffset = lineLen
			}
		}

		np.ByteOffset = n.prefixLen[np.LineNumber-1] + np.ColumnOffset

		return np
	}

	return &cpb.Point{LineNumber: 1}
}

// ByteOffset returns a normalized point based on the given offset within the
// Normalizer's text.  A normalized point has all of its fields set consistently
// and clamped within the range [0,len(text)).
func (n *Normalizer) ByteOffset(offset int32) *cpb.Point {
	np := &cpb.Point{ByteOffset: offset}
	if np.ByteOffset > n.textLen {
		np.ByteOffset = n.textLen
	}

	np.LineNumber = int32(sort.Search(len(n.lineLen), func(i int) bool {
		return n.prefixLen[i] > np.ByteOffset
	}))
	np.ColumnOffset = np.ByteOffset - n.prefixLen[np.LineNumber-1]

	return np
}
