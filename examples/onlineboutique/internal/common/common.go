// SPDX-License-Identifier: MIT
// Copyright (c) 2022 University of California, Riverside

package common

type Transaction struct {
	ID uint32
}

const (
	NodeIDGateway uint8 = iota
	NodeIDFrontend
	NodeIDServiceAd
	NodeIDServiceCart
	NodeIDServiceCurrency
	NodeIDServiceEmail
	NodeIDServicePayment
	NodeIDServiceProductCatalog
	NodeIDServiceRecommendation
	NodeIDServiceShipping
	TotalNodes
)
