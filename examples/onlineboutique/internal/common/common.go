package common

type Transaction struct {
}

const (
	NodeGateway uint8 = iota
	NodeFrontend
	NodeServiceAd
	NodeServiceCart
	NodeServiceCurrency
	NodeServiceEmail
	NodeServicePayment
	NodeServiceProductCatalog
	NodeServiceRecommendation
	NodeServiceShipping
	TotalNodes
)
