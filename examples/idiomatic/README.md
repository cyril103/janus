# Corpus Janus explicite et idiomatique

Ce répertoire contient les sources exécutables du
[guide de style idiomatique](../../docs/idiomatic-janus.md). Pour chacun des six
scénarios, `*_explicit.janus` privilégie les annotations pédagogiques et
`*_idiomatic.janus` applique les conventions du guide. Les deux programmes
sont compilés et exécutés séparément par CTest contre une sortie canonique.

Les variantes idiomatiques utilisent uniquement la syntaxe déjà acceptée par
le compilateur : corps d'expression `=>`, inférence des variables locales et
paramètres de lambda inférés par le contexte et `using val` pour les tableaux
locaux. Elles préservent les contrats de transfert, d’emprunt et de nettoyage ;
une annotation contextuellement inférable peut disparaître, pas le droit
qu’elle représente.
